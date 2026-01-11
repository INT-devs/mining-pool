<?php
/**
 * INTcoin GitHub Webhook Handler
 *
 * This script handles GitHub webhook events for automatic deployment
 * of the INTcoin pool dashboard.
 *
 * Setup:
 * 1. Place this file in /var/www/html/webhook.php
 * 2. Create logs directory: mkdir -p /var/www/html/logs
 * 3. Set permissions: chown -R www-data:www-data /var/www/html/logs
 * 4. Configure GitHub webhook to point to: https://your-domain.com/webhook.php
 * 5. Set the same secret in both GitHub and WEBHOOK_SECRET below
 */

// Configuration
define('WEBHOOK_SECRET', getenv('GITHUB_WEBHOOK_SECRET') ?: 'a81f1f34e08baa572e0321ab12dc1e71ba8c9b84ec8dea8a9301eeb7a5a1323e');
define('REPO_PATH', '/var/www/html');
define('LOG_DIR', '/var/www/html/logs');
define('BRANCH', 'main');

// Ensure logs directory exists
if (!is_dir(LOG_DIR)) {
    mkdir(LOG_DIR, 0755, true);
}

// Log file with date
$logFile = LOG_DIR . '/webhook-' . date('Y-m-d') . '.log';

function logMessage($message) {
    global $logFile;
    $timestamp = date('Y-m-d H:i:s');
    file_put_contents($logFile, "[$timestamp] $message\n", FILE_APPEND | LOCK_EX);
}

function verifySignature($payload, $signature) {
    if (empty($signature)) {
        return false;
    }

    $hash = 'sha256=' . hash_hmac('sha256', $payload, WEBHOOK_SECRET);
    return hash_equals($hash, $signature);
}

// Only accept POST requests
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    logMessage("ERROR: Method not allowed - {$_SERVER['REQUEST_METHOD']}");
    exit('Method not allowed');
}

// Get the payload
$payload = file_get_contents('php://input');
$signature = $_SERVER['HTTP_X_HUB_SIGNATURE_256'] ?? '';

// Verify signature
if (!verifySignature($payload, $signature)) {
    http_response_code(403);
    logMessage("ERROR: Invalid signature");
    exit('Invalid signature');
}

// Parse the payload
$data = json_decode($payload, true);
if (json_last_error() !== JSON_ERROR_NONE) {
    http_response_code(400);
    logMessage("ERROR: Invalid JSON payload");
    exit('Invalid JSON');
}

// Get event type
$event = $_SERVER['HTTP_X_GITHUB_EVENT'] ?? 'unknown';
logMessage("Received event: $event");

// Handle different events
switch ($event) {
    case 'push':
        handlePush($data);
        break;
    case 'release':
        handleRelease($data);
        break;
    case 'workflow_run':
        handleWorkflowRun($data);
        break;
    case 'ping':
        logMessage("Ping received - webhook configured successfully");
        echo "Pong!";
        break;
    default:
        logMessage("Ignored event: $event");
        echo "Event ignored";
}

function handlePush($data) {
    $ref = $data['ref'] ?? '';
    $branch = str_replace('refs/heads/', '', $ref);

    logMessage("Push to branch: $branch");

    if ($branch !== BRANCH) {
        logMessage("Ignoring push to non-main branch");
        echo "Ignored - not main branch";
        return;
    }

    // Check if web/pool-dashboard files changed
    $commits = $data['commits'] ?? [];
    $dashboardChanged = false;

    foreach ($commits as $commit) {
        $files = array_merge(
            $commit['added'] ?? [],
            $commit['modified'] ?? [],
            $commit['removed'] ?? []
        );

        foreach ($files as $file) {
            if (strpos($file, 'web/pool-dashboard/') === 0) {
                $dashboardChanged = true;
                break 2;
            }
        }
    }

    if (!$dashboardChanged) {
        logMessage("No dashboard files changed - skipping deployment");
        echo "No dashboard changes";
        return;
    }

    // Deploy the dashboard
    deployDashboard();
}

function handleRelease($data) {
    $action = $data['action'] ?? '';

    if ($action !== 'published') {
        logMessage("Ignored release action: $action");
        return;
    }

    $release = $data['release'] ?? [];
    $tagName = $release['tag_name'] ?? 'unknown';

    logMessage("New release published: $tagName");

    // Update version in dashboard
    updateVersionFile($tagName);
}

function handleWorkflowRun($data) {
    $action = $data['action'] ?? '';
    $workflow = $data['workflow_run'] ?? [];
    $conclusion = $workflow['conclusion'] ?? '';
    $name = $workflow['name'] ?? 'unknown';

    if ($action !== 'completed' || $conclusion !== 'success') {
        logMessage("Workflow '$name' - action: $action, conclusion: $conclusion");
        return;
    }

    // If it's a test workflow, update test results
    if (stripos($name, 'test') !== false || stripos($name, 'ci') !== false) {
        logMessage("Test workflow completed successfully - updating test results");
        updateTestResults();
    }
}

function deployDashboard() {
    logMessage("Starting dashboard deployment...");

    // Pull latest changes
    $output = [];
    $returnCode = 0;

    // Change to repo directory and pull
    chdir(REPO_PATH);

    exec('git fetch origin ' . BRANCH . ' 2>&1', $output, $returnCode);
    logMessage("Git fetch: " . implode("\n", $output));

    if ($returnCode !== 0) {
        logMessage("ERROR: Git fetch failed with code $returnCode");
        http_response_code(500);
        echo "Deployment failed";
        return;
    }

    $output = [];
    exec('git reset --hard origin/' . BRANCH . ' 2>&1', $output, $returnCode);
    logMessage("Git reset: " . implode("\n", $output));

    if ($returnCode !== 0) {
        logMessage("ERROR: Git reset failed with code $returnCode");
        http_response_code(500);
        echo "Deployment failed";
        return;
    }

    // Copy dashboard files to web root
    $dashboardPath = REPO_PATH . '/web/pool-dashboard';
    if (is_dir($dashboardPath)) {
        $output = [];
        exec("cp -r $dashboardPath/* " . REPO_PATH . "/ 2>&1", $output, $returnCode);
        logMessage("Copy dashboard files: " . implode("\n", $output));
    }

    logMessage("Dashboard deployment completed successfully");
    echo "Deployed successfully";
}

function updateVersionFile($version) {
    $versionFile = REPO_PATH . '/version.json';
    $data = [
        'version' => $version,
        'updated' => date('c')
    ];

    file_put_contents($versionFile, json_encode($data, JSON_PRETTY_PRINT));
    logMessage("Updated version file to $version");
}

function updateTestResults() {
    // This function fetches test results from GitHub Actions
    // In practice, the CI workflow should update test-results.json directly
    logMessage("Test results should be updated by CI workflow");
}

?>
