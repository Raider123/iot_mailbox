<?php
/* ---------- Konfiguration ---------------------------------- */
const API_KEY = 'tPmAT5Ab3j7F9';          // muss mit ESP32 übereinstimmen
const DB_HOST = 'postkasten.lima-db.de';  // Host aus dem Lima-City-Panel
const DB_USER = 'USER449560';
const DB_PASS = '1234briefkasten';
const DB_NAME = 'db_449560_1';
const NTFY_URL = 'https://ntfy.sh/briefkasten123';   // dein Topic

error_reporting(E_ALL);
ini_set('display_errors', 1);

/* ---------- DB-Verbindung ---------------------------------- */
$db = new mysqli(DB_HOST, DB_USER, DB_PASS, DB_NAME);
if ($db->connect_error) {
    http_response_code(500);
    exit('DB-FAIL');
}
$db->set_charset('utf8mb4');              // UTF-8 für Emojis & Umlaute

/* ---------- JSON-Ausgabe fürs Frontend --------------------- */
if (($_GET['view'] ?? '') === 'json') {
    $res = $db->query(
        "SELECT ts, text 
           FROM events 
       ORDER BY id DESC 
          LIMIT 50"
    );
    header('Content-Type: application/json');
    echo json_encode($res->fetch_all(MYSQLI_ASSOC));
    exit;
}

/* ---------- POST-Endpoint für den ESP32 -------------------- */
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);   // nur POST erlaubt
    exit;
}

if (($_POST['api_key'] ?? '') !== API_KEY) {
    http_response_code(403);   // falscher Schlüssel
    exit('KEY');
}

$msg = trim($_POST['msg'] ?? '');
if ($msg === '') {
    http_response_code(422);   // leere Nachricht
    exit('MSG');
}

$stmt = $db->prepare(
    "INSERT INTO events (ts, text) VALUES (NOW(), ?)"
);
$stmt->bind_param('s', $msg);
$stmt->execute();

echo 'STORED';                 // Erfolg

echo 'STORED';   // Erfolg wurde schon an ESP32 geschickt

/* Push nur bei “MAIL” – erst jetzt ausführen */
if ($msg === 'MAIL') {
    $ch = curl_init(NTFY_URL);
    curl_setopt_array($ch, [
        CURLOPT_POST           => 1,
        CURLOPT_POSTFIELDS     => '📬 Neue Post im Briefkasten!',
        CURLOPT_HTTPHEADER     => [
            'Title: Postkasten'
        ],
        CURLOPT_TIMEOUT        => 3,        // blockiert das Skript nicht lang
        CURLOPT_RETURNTRANSFER => true
    ]);
    curl_exec($ch);
    curl_close($ch);
}

?>
