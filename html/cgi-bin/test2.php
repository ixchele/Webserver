<?php
session_start();

if (isset($_GET['action']) && $_GET['action'] == 'logout') {
    $_SESSION = array();
    if (ini_get("session.use_cookies")) {
        $params = session_get_cookie_params();
        setcookie(session_name(), '', time() - 42000,
            $params["path"], $params["domain"],
            $params["secure"], $params["httponly"]
        );
    }
    session_destroy();
    header("Location: test2.php");
    exit;
}

if ($_SERVER['REQUEST_METHOD'] == 'POST' && !empty($_POST['username'])) {
    $_SESSION['username'] = $_POST['username'];
    $_SESSION['age'] = !empty($_POST['age']) ? $_POST['age'] : 'N/A';
    $_SESSION['promo'] = !empty($_POST['promo']) ? $_POST['promo'] : 'N/A';
    $_SESSION['city'] = !empty($_POST['city']) ? $_POST['city'] : 'N/A';
    if (!isset($_SESSION['login_time'])) {
        $_SESSION['login_time'] = time();
    }
    header("Location: test2.php");
    exit;
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Webserv Session Validation</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700&display=swap');
        body { font-family: 'Orbitron', sans-serif; overflow: hidden; }
        .star { position: absolute; background-color: white; border-radius: 50%; animation: twinkle linear infinite; }
        .main-title { animation: float 6s ease-in-out infinite; text-shadow: 0 0 15px rgba(234, 179, 8, 0.7), 0 0 30px rgba(234, 179, 8, 0.5); }
        @keyframes float { 0%, 100% { transform: translateY(0px); } 50% { transform: translateY(-15px); } }
        @keyframes twinkle { 0%, 100% { opacity: 0; } 50% { opacity: 1; } }
    </style>
</head>
<body class="bg-gray-900 text-white flex items-center justify-center min-h-screen">
    
    <div id="starfield" class="absolute top-0 left-0 w-full h-full"></div>

    <div class="text-center z-10 p-8 bg-black bg-opacity-50 rounded-2xl backdrop-blur-sm border border-yellow-500/30 max-w-lg w-full">
        <h1 class="main-title text-5xl md:text-6xl font-bold text-yellow-400">SESSION STATUS</h1>
        
        <div class="mt-6 text-left bg-gray-800/50 p-4 rounded-lg border border-gray-700 min-h-[150px]">
            <h2 class="text-xl font-bold text-yellow-300 mb-2">Current Session Data:</h2>
            <?php if (isset($_SESSION['username'])): ?>
                <p><strong>Name:</strong> <?php echo htmlspecialchars($_SESSION['username']); ?></p>
                <p><strong>Age:</strong> <?php echo htmlspecialchars($_SESSION['age']); ?></p>
                <p><strong>Promo:</strong> <?php echo htmlspecialchars($_SESSION['promo']); ?></p>
                <p><strong>City:</strong> <?php echo htmlspecialchars($_SESSION['city']); ?></p>
                <p class="mt-2 text-xs text-gray-500"><strong>Session ID (from cookie):</strong> <?php echo htmlspecialchars(session_id()); ?></p>
            <?php else: ?>
                <p class="text-gray-400">No active session found. Enter your details below to begin.</p>
            <?php endif; ?>
        </div>

        <form action="test2.php" method="post" class="mt-6 text-left">
            <label for="username" class="block mb-2 text-sm font-medium">Name:</label>
            <input type="text" name="username" id="username" placeholder="Enter name to start/update session" class="bg-gray-800 border border-gray-600 text-white text-sm rounded-lg block w-full p-2.5" required>
            
            <label for="age" class="block mt-4 mb-2 text-sm font-medium">Age:</label>
            <input type="text" name="age" id="age" class="bg-gray-800 border border-gray-600 text-white text-sm rounded-lg block w-full p-2.5">
            
            <label for="promo" class="block mt-4 mb-2 text-sm font-medium">Promo:</label>
            <input type="text" name="promo" id="promo" class="bg-gray-800 border border-gray-600 text-white text-sm rounded-lg block w-full p-2.5">

            <label for="city" class="block mt-4 mb-2 text-sm font-medium">City:</label>
            <input type="text" name="city" id="city" class="bg-gray-800 border border-gray-600 text-white text-sm rounded-lg block w-full p-2.5">

            <button type="submit" class="mt-6 w-full bg-yellow-600 hover:bg-yellow-700 text-white font-bold py-3 px-6 rounded-lg transition duration-300">
                Save Session Data
            </button>
        </form>

        <div class="mt-4">
             <?php if (isset($_SESSION['username'])): ?>
                <a href="test2.php?action=logout" class="text-red-400 hover:text-red-500 text-sm">Logout (Destroy Session)</a> |
             <?php endif; ?>
             <a href="/" class="text-sky-400 hover:text-sky-500 text-sm">Return to Homepage</a>
        </div>
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
