<?php
// This part must be at the very top, before any HTML is sent.
header("Content-Type: text/html");

// --- UNIVERSAL BODY HANDLING ---
$upload_message = "";
$post_data_detected = false;
$file_data_detected = false;
$raw_body_data = "";

// 1. Check for File Uploads first (multipart/form-data)
if (!empty($_FILES)) {
    $file_data_detected = true;
    if (isset($_FILES["fileToUpload"]) && $_FILES["fileToUpload"]["error"] == UPLOAD_ERR_OK) {
        $target_dir = "uploads/";
        if (!is_dir($target_dir)) {
            if (!mkdir($target_dir, 0755, true)) {
                 $upload_message = "<div class='message error'>❌ Error: The 'uploads/' directory does not exist and could not be created. Please create it manually inside the cgi-bin directory.</div>";
            }
        }

        if (is_dir($target_dir) && is_writable($target_dir)) {
            $target_file = $target_dir . basename($_FILES["fileToUpload"]["name"]);
            if (move_uploaded_file($_FILES["fileToUpload"]["tmp_name"], $target_file)) {
                $upload_message = "<div class='message success'>✅ Success: File saved to <code>" . htmlspecialchars($target_file) . "</code> on the server.</div>";
            } else {
                $upload_message = "<div class='message error'>❌ Error: Could not move the uploaded file. Check server permissions for the <code>" . $target_dir . "</code> directory.</div>";
            }
        } else if (empty($upload_message)) {
            $upload_message = "<div class='message error'>❌ Error: The <code>uploads/</code> directory is not writable. Please check its permissions (e.g., chmod 755).</div>";
        }
    } else if (isset($_FILES["fileToUpload"]) && $_FILES["fileToUpload"]["error"] != UPLOAD_ERR_NO_FILE) {
        $upload_message = "<div class='message error'>❌ Upload Error: Code " . $_FILES["fileToUpload"]["error"] . ". The file could not be uploaded. This might be due to server configuration (php.ini limits) or permissions on the temporary upload directory (e.g. /tmp).</div>";
    }
}

// 2. Check for standard POST data (x-www-form-urlencoded or from multipart)
if (!empty($_POST)) {
    $post_data_detected = true;
}

// 3. As a fallback, try to read the raw body.
// This works for types like application/json, text/plain, etc.
// Note: It will be EMPTY if PHP has already parsed the body into $_POST or $_FILES.
$raw_body_data = file_get_contents('php://input');

?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Webserv CGI Test</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700&display=swap');
        body { font-family: 'Orbitron', sans-serif; }
        .star { position: absolute; background-color: white; border-radius: 50%; animation: twinkle linear infinite; }
        .main-title { animation: float 6s ease-in-out infinite; text-shadow: 0 0 15px rgba(77, 145, 255, 0.7), 0 0 30px rgba(77, 145, 255, 0.5); }
        .rocket { animation: rocket-fly 10s ease-in-out infinite; }
        @keyframes float { 0%, 100% { transform: translateY(0px); } 50% { transform: translateY(-10px); } }
        @keyframes twinkle { 0%, 100% { opacity: 0; } 50% { opacity: 1; } }
        @keyframes rocket-fly { 0% { transform: translateY(10px) rotate(-45deg); } 50% { transform: translateY(-10px) rotate(-45deg); } 100% { transform: translateY(10px) rotate(-45deg); } }
        
        h2 { color: #bb86fc; border-bottom: 1px solid #bb86fc; padding-bottom: 8px; margin-top: 2rem; }
        table { border-collapse: collapse; width: 100%; margin-top: 20px; }
        th, td { text-align: left; padding: 12px; border: 1px solid #4a5568; word-break: break-all; }
        th { background-color: #bb86fc; color: #121212; }
        tr:nth-child(even) { background-color: rgba(45, 55, 72, 0.5); }
        pre { background-color: #2d3748; padding: 15px; border-radius: 5px; white-space: pre-wrap; word-wrap: break-word; border: 1px solid #4a5568; }
        .method { padding: 8px 12px; border-radius: 5px; font-weight: bold; display: inline-block; margin: 10px 0; }
        .get { background-color: #03dac6; color: #121212; }
        .post { background-color: #cf6679; color: #121212; }
        form { margin-top: 20px; padding: 20px; background: rgba(45, 55, 72, 0.5); border-radius: 8px; border: 1px solid #4a5568; }
        input[type="text"], input[type="file"] { width: calc(100% - 24px); padding: 10px; margin-bottom: 10px; border-radius: 4px; border: 1px solid #718096; background: #2d3748; color: #e2e8f0;}
        input[type="submit"] { background-color: #bb86fc; color: #121212; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; font-weight: bold; }
        .message { padding: 15px; margin-top: 20px; border-radius: 5px; font-size: 1rem; }
        .success { background-color: rgba(76, 175, 80, 0.2); color: #81c784; border: 1px solid #4caf50; }
        .error { background-color: rgba(207, 102, 121, 0.2); color: #e57373; border: 1px solid #cf6679; }
        code { background-color: rgba(187, 134, 252, 0.2); padding: 2px 5px; border-radius: 4px; font-family: monospace; }
    </style>
</head>
<body class="bg-gray-900 text-gray-300">
    
    <div id="starfield" class="fixed top-0 left-0 w-full h-full z-0"></div>

    <div class="relative z-10 p-6 md:p-8 my-8 max-w-4xl mx-auto bg-black bg-opacity-60 rounded-2xl backdrop-blur-sm border border-blue-500/30">
        <div class="relative text-center mb-8">
            <h1 class="main-title text-5xl md:text-6xl font-bold text-blue-400">UNIVERSAL CGI PROBE</h1>
            <div class="rocket absolute -top-8 -right-8 text-5xl">🛰️</div>
        </div>
        
        <?php if (!empty($upload_message)) { echo "<h2>Upload Status</h2>" . $upload_message; } ?>

        <h2>Request Method</h2>
        <p class="method <?php echo strtolower($_SERVER['REQUEST_METHOD']); ?>"><?php echo htmlspecialchars($_SERVER['REQUEST_METHOD']); ?></p>

        <h2>POST Data ($_POST)</h2>
        <pre><?php if ($post_data_detected) { print_r($_POST); } else { echo "No standard POST data detected."; } ?></pre>
        
        <h2>Uploaded Files Data ($_FILES)</h2>
        <pre><?php if ($file_data_detected) { print_r($_FILES); } else { echo "No files detected in upload."; } ?></pre>

        <h2>Raw Request Body (from stdin)</h2>
        <pre><?php
            if (strlen($raw_body_data) > 0) { 
                echo htmlspecialchars($raw_body_data); 
            } else { 
                echo "No raw body data read from stdin.\n(This is normal for multipart/form-data as PHP parses it automatically)"; 
            }
        ?></pre>
        
        <h2>Environment Variables</h2>
        <table>
            <tr><th>Variable Name</th><th>Value</th></tr>
            <?php
                foreach ($_SERVER as $key => $value) {
                    echo "<tr><td>" . htmlspecialchars($key) . "</td><td>" . htmlspecialchars($value) . "</td></tr>";
                }
            ?>
        </table>

        <h2>Test Form</h2>
        <p>Use this form to send a <code>multipart/form-data</code> request.</p>
        <form action="test.php" method="post" enctype="multipart/form-data">
            <label for="name">Text Field:</label><br>
            <input type="text" id="name" name="username" value="ebouboul"><br><br>
            <label for="file">File Upload:</label><br>
            <input type="file" name="fileToUpload" id="fileToUpload"><br><br>
            <input type="submit" value="Launch POST Probe">
        </form>
    </div>

    <script>
        const starfield = document.getElementById('starfield');
        for (let i = 0; i < 200; i++) {
            const star = document.createElement('div');
            star.className = 'star';
            const size = Math.random() * 2 + 1;
            star.style.width = `${size}px`;
            star.style.height = `${size}px`;
            star.style.top = `${Math.random() * 100}%`;
            star.style.left = `${Math.random() * 100}%`;
            star.style.animationDuration = `${Math.random() * 3 + 2}s`;
            star.style.animationDelay = `${Math.random() * 3}s`;
            starfield.appendChild(star);
        }
    </script>
</body>
</html>
