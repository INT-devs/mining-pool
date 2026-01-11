<?php
/**
 * INTcoin GitHub Webhook Handler
 *
 * Handles multiple GitHub webhook events:
 * - Push events: Updates roadmap and deploys pool dashboard
 * - Release events: Updates version.json
 * - Workflow run events: Updates test results
 * - Ping events: Verifies webhook configuration
 *
 * Setup Instructions:
 * 1. Go to https://github.com/INT-devs/intcoin/settings/hooks
 * 2. Add webhook with URL: https://international-coin.org/webhooks/update-roadmap.php
 * 3. Set Content type: application/json
 * 4. Set Secret: (use the configured secret below)
 * 5. Select events: Push, Releases, Workflow runs
 * 6. Ensure Active is checked
 */

// Configuration
define('WEBHOOK_SECRET', getenv('GITHUB_WEBHOOK_SECRET') ?: 'a81f1f34e08baa572e0321ab12dc1e71ba8c9b84ec8dea8a9301eeb7a5a1323e');
define('GITHUB_RAW_URL', 'https://raw.githubusercontent.com/INT-devs/intcoin/main/ROADMAP.md');
define('WEB_ROOT', '/var/www/html');
define('CACHE_DIR', WEB_ROOT . '/cache');
define('LOG_DIR', WEB_ROOT . '/logs');
define('BRANCH', 'main');

// Ensure directories exist
if (!is_dir(LOG_DIR)) {
    mkdir(LOG_DIR, 0755, true);
}
if (!is_dir(CACHE_DIR)) {
    mkdir(CACHE_DIR, 0755, true);
}

// Log file with date
$logFile = LOG_DIR . '/webhook-' . date('Y-m-d') . '.log';

// Enable error logging
ini_set('display_errors', 0);
ini_set('log_errors', 1);
error_reporting(E_ALL);

/**
 * Log message to file
 */
function logMessage($message) {
    global $logFile;
    $timestamp = date('Y-m-d H:i:s');
    file_put_contents($logFile, "[$timestamp] $message\n", FILE_APPEND | LOCK_EX);
}

/**
 * Send JSON response and exit
 */
function sendResponse($status, $message) {
    http_response_code($status);
    header('Content-Type: application/json');
    echo json_encode(['status' => $status === 200 ? 'success' : 'error', 'message' => $message]);
    exit;
}

/**
 * Verify GitHub webhook signature
 */
function verifySignature($payload, $signature) {
    if (empty($signature)) {
        return false;
    }
    $expected = 'sha256=' . hash_hmac('sha256', $payload, WEBHOOK_SECRET);
    return hash_equals($expected, $signature);
}

/**
 * Parse Markdown to HTML (basic implementation)
 */
function markdownToHTML($markdown) {
    // Headers
    $html = preg_replace('/^### (.*?)$/m', '<h3>$1</h3>', $markdown);
    $html = preg_replace('/^## (.*?)$/m', '<h2>$1</h2>', $html);
    $html = preg_replace('/^# (.*?)$/m', '<h1>$1</h1>', $html);

    // Bold and italic
    $html = preg_replace('/\*\*(.*?)\*\*/', '<strong>$1</strong>', $html);
    $html = preg_replace('/\*(.*?)\*/', '<em>$1</em>', $html);

    // Links
    $html = preg_replace('/\[(.*?)\]\((.*?)\)/', '<a href="$2">$1</a>', $html);

    // Checkboxes
    $html = preg_replace('/- \[x\] (.*)$/m', '<li class="completed">$1</li>', $html);
    $html = preg_replace('/- \[ \] (.*)$/m', '<li class="pending">$1</li>', $html);

    // Lists
    $html = preg_replace('/^\* (.*)$/m', '<li>$1</li>', $html);
    $html = preg_replace('/(<li[^>]*>.*<\/li>\n?)+/s', '<ul>$0</ul>', $html);

    // Line breaks
    $html = preg_replace('/\n\n/', '</p><p>', $html);
    $html = '<p>' . $html . '</p>';

    // Clean up empty paragraphs
    $html = preg_replace('/<p>\s*<\/p>/', '', $html);

    return $html;
}

/**
 * Fetch file from GitHub
 */
function fetchFromGitHub($url) {
    $context = stream_context_create([
        'http' => [
            'user_agent' => 'INTcoin-Webhook/1.0',
            'timeout' => 10
        ]
    ]);

    $content = @file_get_contents($url, false, $context);

    if ($content === false) {
        $error = error_get_last();
        throw new Exception('Failed to fetch from GitHub: ' . ($error['message'] ?? 'Unknown error'));
    }

    return $content;
}

/**
 * Handle push events
 */
function handlePush($data) {
    $ref = $data['ref'] ?? '';
    $branch = str_replace('refs/heads/', '', $ref);

    logMessage("Push to branch: $branch");

    if ($branch !== BRANCH) {
        logMessage("Ignoring push to non-main branch");
        return "Ignored - not main branch";
    }

    $commits = $data['commits'] ?? [];
    $roadmapModified = false;
    $dashboardChanged = false;

    foreach ($commits as $commit) {
        $files = array_merge(
            $commit['added'] ?? [],
            $commit['modified'] ?? [],
            $commit['removed'] ?? []
        );

        foreach ($files as $file) {
            if ($file === 'ROADMAP.md') {
                $roadmapModified = true;
            }
            if (strpos($file, 'web/pool-dashboard/') === 0) {
                $dashboardChanged = true;
            }
        }
    }

    $results = [];

    // Update roadmap if modified
    if ($roadmapModified) {
        try {
            logMessage('Fetching ROADMAP.md from GitHub');
            $markdown = fetchFromGitHub(GITHUB_RAW_URL);

            logMessage('Converting Markdown to HTML');
            $html = markdownToHTML($markdown);

            $cacheFile = CACHE_DIR . '/roadmap.html';
            $fullHTML = "<div class=\"roadmap-content\">\n    $html\n</div>";

            if (file_put_contents($cacheFile, $fullHTML) !== false) {
                logMessage('Roadmap updated successfully');
                $results[] = 'Roadmap updated';
            }
        } catch (Exception $e) {
            logMessage('Roadmap update failed: ' . $e->getMessage());
            $results[] = 'Roadmap update failed: ' . $e->getMessage();
        }
    }

    // Deploy dashboard if changed
    if ($dashboardChanged) {
        $results[] = deployDashboard();
    }

    if (empty($results)) {
        return "No relevant files changed";
    }

    return implode('; ', $results);
}

/**
 * Deploy pool dashboard files
 */
function deployDashboard() {
    logMessage("Starting dashboard deployment...");

    // Pull latest changes via git
    $output = [];
    $returnCode = 0;

    chdir(WEB_ROOT);

    exec('git fetch origin ' . BRANCH . ' 2>&1', $output, $returnCode);
    logMessage("Git fetch: " . implode("\n", $output));

    if ($returnCode !== 0) {
        logMessage("ERROR: Git fetch failed with code $returnCode");
        return "Dashboard deployment failed (git fetch)";
    }

    $output = [];
    exec('git reset --hard origin/' . BRANCH . ' 2>&1', $output, $returnCode);
    logMessage("Git reset: " . implode("\n", $output));

    if ($returnCode !== 0) {
        logMessage("ERROR: Git reset failed with code $returnCode");
        return "Dashboard deployment failed (git reset)";
    }

    // Copy dashboard files to web root
    $dashboardPath = WEB_ROOT . '/web/pool-dashboard';
    if (is_dir($dashboardPath)) {
        $output = [];
        exec("cp -r $dashboardPath/* " . WEB_ROOT . "/ 2>&1", $output, $returnCode);
        logMessage("Copy dashboard files: " . implode("\n", $output));

        if ($returnCode !== 0) {
            logMessage("ERROR: Copy failed with code $returnCode");
            return "Dashboard deployment failed (copy)";
        }
    }

    logMessage("Dashboard deployment completed successfully");
    return "Dashboard deployed";
}

/**
 * Handle release events
 */
function handleRelease($data) {
    $action = $data['action'] ?? '';

    if ($action !== 'published') {
        logMessage("Ignored release action: $action");
        return "Release action ignored";
    }

    $release = $data['release'] ?? [];
    $tagName = $release['tag_name'] ?? 'unknown';
    $prerelease = $release['prerelease'] ?? false;

    logMessage("New release published: $tagName (prerelease: " . ($prerelease ? 'yes' : 'no') . ")");

    // Update version.json
    $versionFile = WEB_ROOT . '/version.json';
    $versionData = [
        'version' => $tagName,
        'prerelease' => $prerelease,
        'updated' => date('c'),
        'release_url' => $release['html_url'] ?? ''
    ];

    if (file_put_contents($versionFile, json_encode($versionData, JSON_PRETTY_PRINT)) !== false) {
        logMessage("Updated version file to $tagName");
        return "Version updated to $tagName";
    }

    logMessage("Failed to update version file");
    return "Version update failed";
}

/**
 * Handle workflow run events
 */
function handleWorkflowRun($data) {
    $action = $data['action'] ?? '';
    $workflow = $data['workflow_run'] ?? [];
    $conclusion = $workflow['conclusion'] ?? '';
    $name = $workflow['name'] ?? 'unknown';

    if ($action !== 'completed') {
        logMessage("Workflow '$name' action: $action (not completed)");
        return "Workflow not completed";
    }

    logMessage("Workflow '$name' completed with conclusion: $conclusion");

    // If it's a test workflow, update test results
    if (stripos($name, 'test') !== false || stripos($name, 'ci') !== false) {
        if ($conclusion === 'success') {
            logMessage("Test workflow completed successfully");

            // Fetch test results from workflow artifacts if available
            // For now, just update the timestamp to indicate tests passed
            $testFile = WEB_ROOT . '/test-results.json';
            $testData = [];

            if (file_exists($testFile)) {
                $testData = json_decode(file_get_contents($testFile), true) ?: [];
            }

            $testData['lastUpdated'] = date('c');
            $testData['lastConclusion'] = $conclusion;
            $testData['workflowName'] = $name;

            file_put_contents($testFile, json_encode($testData, JSON_PRETTY_PRINT));

            return "Test results updated";
        } else {
            logMessage("Test workflow failed: $conclusion");
            return "Test workflow failed: $conclusion";
        }
    }

    return "Workflow event processed";
}

// ========== Main Execution ==========

try {
    logMessage('Webhook received from ' . ($_SERVER['REMOTE_ADDR'] ?? 'unknown'));

    // Only accept POST requests
    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        logMessage("ERROR: Method not allowed - {$_SERVER['REQUEST_METHOD']}");
        sendResponse(405, 'Method not allowed');
    }

    // Get payload
    $payload = file_get_contents('php://input');
    $signature = $_SERVER['HTTP_X_HUB_SIGNATURE_256'] ?? '';

    // Verify signature
    if (!verifySignature($payload, $signature)) {
        logMessage('Invalid signature');
        sendResponse(403, 'Invalid signature');
    }

    // Parse payload
    $data = json_decode($payload, true);
    if (json_last_error() !== JSON_ERROR_NONE) {
        logMessage('Invalid JSON payload');
        sendResponse(400, 'Invalid JSON payload');
    }

    // Get event type
    $event = $_SERVER['HTTP_X_GITHUB_EVENT'] ?? 'unknown';
    logMessage("Received event: $event");

    // Handle different events
    $result = '';
    switch ($event) {
        case 'push':
            $result = handlePush($data);
            break;
        case 'release':
            $result = handleRelease($data);
            break;
        case 'workflow_run':
            $result = handleWorkflowRun($data);
            break;
        case 'ping':
            logMessage("Ping received - webhook configured successfully");
            $result = "Pong! Webhook configured successfully.";
            break;
        default:
            logMessage("Ignored event: $event");
            $result = "Event ignored: $event";
    }

    logMessage("Result: $result");
    sendResponse(200, $result);

} catch (Exception $e) {
    logMessage('Error: ' . $e->getMessage());
    sendResponse(500, $e->getMessage());
}
