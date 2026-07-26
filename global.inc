<?php
include_once("settings.inc");

// Session holds the OpenKJ user session token. Must be started before any
// output, which is also why every header() call has to precede siteheader().
if (session_status() === PHP_SESSION_NONE) session_start();


/* ------------------------------------------------------------------ *
 * OpenKJ Local Mode API client
 * ------------------------------------------------------------------ */

// Normalized shape for every call: ok, status, data, error.
function okj_failure($message, $status = 0)
{
  return array('ok' => false, 'status' => $status, 'data' => array(), 'error' => $message);
}

// The API reports logical failures two different ways depending on the route
// family: /local/* returns {"ok":false,"error":"..."} while the legacy
// /api.php commands return {"error":"true","errorString":"..."}.
function okj_interpret($data, $status)
{
  if (array_key_exists('ok', $data))
  {
    $ok = (bool)$data['ok'];
    $error = $ok ? '' : (isset($data['error']) ? $data['error'] : 'Request failed');
  }
  elseif (array_key_exists('error', $data) && is_string($data['error']))
  {
    $ok = ($data['error'] === 'false');
    $error = $ok ? '' : (isset($data['errorString']) ? $data['errorString'] : 'Request failed');
  }
  else
  {
    $ok = ($status >= 200 && $status < 300);
    $error = $ok ? '' : 'Request failed';
  }
  if ($status < 200 || $status >= 300)
  {
    $ok = false;
    if ($error === '') $error = "KJ software returned HTTP $status";
  }
  return array('ok' => $ok, 'status' => $status, 'data' => $data, 'error' => $error);
}

function okj_call($method, $path, $body = null, $query = array())
{
  global $openkjApiBase, $openkjTimeout;

  $url = rtrim($openkjApiBase, '/') . $path;
  if ($query) $url .= '?' . http_build_query($query);
  $payload = ($method === 'POST') ? json_encode($body === null ? new stdClass() : $body) : null;

  if (function_exists('curl_init'))
  {
    $ch = curl_init($url);
    curl_setopt_array($ch, array(
      CURLOPT_RETURNTRANSFER => true,
      CURLOPT_CONNECTTIMEOUT => 2,
      CURLOPT_TIMEOUT => $openkjTimeout,
      CURLOPT_HTTPHEADER => array('Content-Type: application/json', 'Accept: application/json'),
    ));
    if ($method === 'POST')
    {
      curl_setopt($ch, CURLOPT_POST, true);
      curl_setopt($ch, CURLOPT_POSTFIELDS, $payload);
    }
    $raw = curl_exec($ch);
    $status = (int)curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
    $curlError = curl_error($ch);
    curl_close($ch);
    if ($raw === false) return okj_failure($curlError ? $curlError : 'Could not reach the KJ software');
  }
  else
  {
    $options = array('http' => array(
      'method' => $method,
      'header' => "Content-Type: application/json\r\nAccept: application/json\r\n",
      'timeout' => $openkjTimeout,
      'ignore_errors' => true,
    ));
    if ($method === 'POST') $options['http']['content'] = $payload;
    $raw = @file_get_contents($url, false, stream_context_create($options));
    if ($raw === false) return okj_failure('Could not reach the KJ software');
    $status = 0;
    if (isset($http_response_header[0]) &&
        preg_match('!HTTP/\S+\s+(\d+)!', $http_response_header[0], $m)) $status = (int)$m[1];
  }

  $data = json_decode($raw, true);
  if (!is_array($data)) return okj_failure('Unexpected response from the KJ software', $status);
  return okj_interpret($data, $status);
}

function okj_get($path, $query = array())
{
  return okj_call('GET', $path, null, $query);
}

function okj_post($path, $body = array())
{
  return okj_call('POST', $path, $body);
}


/* ------------------------------------------------------------------ *
 * Session / current user
 * ------------------------------------------------------------------ */

function okj_token()
{
  return isset($_SESSION['okj_token']) ? $_SESSION['okj_token'] : '';
}

// Cached for the life of the request: every page that renders nav needs it.
function okj_user($refresh = false)
{
  static $user = null;
  if ($refresh) $user = null;
  if ($user !== null) return $user;

  $user = array('authenticated' => false, 'username' => '', 'inRotation' => false, 'away' => false);
  $token = okj_token();
  if ($token === '') return $user;

  $res = okj_get('/local/user/me', array('token' => $token));
  if (!$res['ok'] || empty($res['data']['authenticated']))
  {
    // Token expired or was revoked on the OpenKJ side.
    unset($_SESSION['okj_token']);
    return $user;
  }

  $user = array(
    'authenticated' => true,
    'username' => isset($res['data']['username']) ? $res['data']['username'] : '',
    'inRotation' => !empty($res['data']['inRotation']),
    'away' => !empty($res['data']['away']),
  );
  return $user;
}

// Call after mutating anything /local/user/me reports (currently the away flag).
function okj_user_refresh()
{
  return okj_user(true);
}

// Search fires on nearly every keystroke and each render needs this, so it gets
// a few seconds of slack rather than a round trip every time. Going stale for
// that long is harmless: the request itself is still authoritative, and OpenKJ
// rejects one made against a closed queue.
function okj_accepting()
{
  static $accepting = null;
  if ($accepting !== null) return $accepting;

  if (isset($_SESSION['okj_accepting_until']) && $_SESSION['okj_accepting_until'] > time())
  {
    $accepting = (bool)$_SESSION['okj_accepting'];
    return $accepting;
  }

  $res = okj_post('/api.php', array('command' => 'venueAccepting'));
  $accepting = ($res['ok'] && !empty($res['data']['accepting']));
  if ($res['ok'])
  {
    $_SESSION['okj_accepting'] = $accepting;
    $_SESSION['okj_accepting_until'] = time() + 5;
  }
  return $accepting;
}


/* ------------------------------------------------------------------ *
 * Output helpers
 * ------------------------------------------------------------------ */

function h($value)
{
  return htmlspecialchars((string)$value, ENT_QUOTES, 'UTF-8');
}

// JSON safe to sit inside a single-quoted hx-vals attribute.
function hxvals($values)
{
  return h(json_encode($values));
}

// htmx swaps a fragment into an existing page; a direct hit (or an hx-boost'ed
// navigation) needs the whole document. Pages render only their target
// fragment when this is true.
function okj_is_fragment()
{
  return isset($_SERVER['HTTP_HX_REQUEST']) && !isset($_SERVER['HTTP_HX_BOOSTED']);
}

function okj_event_name()
{
  global $venueName;
  static $name = null;
  if ($name !== null) return $name;

  $name = $venueName;
  // Cached per session, but not forever: a KJ who renames the event shouldn't
  // have to ask everyone to clear their cookies.
  if (isset($_SESSION['okj_event_name']) && isset($_SESSION['okj_event_name_until'])
      && $_SESSION['okj_event_name_until'] > time())
  {
    $name = $_SESSION['okj_event_name'];
    return $name;
  }
  $res = okj_get('/local/event-settings');
  if (!$res['ok']) return $name;

  if (!empty($res['data']['appName']) && $res['data']['appName'] !== 'OpenKJ')
  {
    $name = $res['data']['appName'];
  }
  $_SESSION['okj_event_name'] = $name;
  $_SESSION['okj_event_name_until'] = time() + 300;
  return $name;
}

function siteheader()
{
  $eventName = okj_event_name();
  echo "<html>
  <head>
    <meta charset=\"UTF-8\">
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
    <title>" . h($eventName) . " Songbook</title>

    <link rel=\"preconnect\" href=\"https://cdn.jsdelivr.net\" crossorigin>
    <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@xz/fonts@1.0.2/serve/metropolis.css\" integrity=\"sha256-EbF7uuhKup7x84hcfaVXj5gof+v/9hfv7Wsxud7vWyU=\" crossorigin=\"anonymous\">
    <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@fontsource/righteous@5.0.14/index.css\" integrity=\"sha256-3LZETteO5Rdt11DQIU0Uj0He3Tdr/JnfBZcsP87UifE=\" crossorigin=\"anonymous\">

    <link rel=\"stylesheet\" type=\"text/css\" href=\"css/style.css\" />

    <script defer src=\"https://cdn.jsdelivr.net/npm/htmx.org@1.9.6/dist/htmx.min.js\" integrity=\"sha256-y7cjwwXPbWMVyJCQmBVSNYhQni4JKln4z8SohYKWidU=\" crossorigin=\"anonymous\"></script>
  </head>
  <body
    hx-on:keydown=\"
      const reqModal = htmx.find('#req-modal');
      if (event.key === 'Escape' && reqModal)
        htmx.trigger(reqModal, 'remove-req-modal');
    \">";
}

function sitefooter() {
  echo "</main></body></html>";
}

function navbar($active = 'songbook')
{
  $eventName = okj_event_name();
  $user = okj_user();

  echo "<header><h1><a href='/' hx-boost='true'>" . h($eventName) . " Songbook</a></h1>
    <nav hx-boost='true'>";
  $links = array('/' => 'Search', '/browse.php' => 'Browse', '/queue.php' => 'Queue');
  $keys = array('/' => 'songbook', '/browse.php' => 'browse', '/queue.php' => 'queue');
  foreach ($links as $href => $label)
  {
    $current = ($keys[$href] === $active) ? " class='current'" : "";
    echo "<a href='" . h($href) . "'$current>" . h($label) . "</a> ";
  }
  if ($user['authenticated'])
    echo "<a href='/account.php'" . ($active === 'account' ? " class='current'" : "") . ">" . h($user['username']) . "</a>";
  else
    echo "<a href='/account.php'" . ($active === 'account' ? " class='current'" : "") . ">Sign in</a>";
  echo "</nav></header><main>";
}

function errorBanner($message)
{
  if ($message === '') return;
  echo "<p class=\"api-error\">" . h($message) . "</p>";
}

// Shown when the KJ software isn't reachable at all - distinct from an empty
// result, because there is nothing the singer can do about it.
function offlineNotice($message)
{
  echo "<div class=\"offline\">
    <h2>Songbook unavailable</h2>
    <p>Can't reach the karaoke software right now. Please let your KJ know.</p>
    <p class=\"detail\">" . h($message) . "</p>
  </div>";
}


/* ------------------------------------------------------------------ *
 * Shared UI pieces
 * ------------------------------------------------------------------ */

function searchform($query = "")
{
  $disabled = ($query == "") ? "disabled" : "";
  echo '<div id="song-search-input-container"><input
      id="song-search-input"
      type="search"
      name="q"
      value="' . h($query) . '"
      autofocus
      autocomplete="off"
      placeholder="Type here to search songs"
      hx-get="/search.php"
      hx-sync="this:replace"
      hx-push-url="true"
      hx-trigger="input[this.value.trim().length > 2 || this.value.length === 0] changed delay:250ms, search"
      hx-target="#data-target"
      hx-select="#data-target"
      hx-indicator="#search-indicator"
      hx-on:clear-search="
        this.value = \'\';
        htmx.trigger(this, \'input\');
        htmx.find(\'#song-search-input-clear\').setAttribute(\'disabled\', \'true\');
      "
      hx-on:input="
        if (this.value.length > 0)
          htmx.find(\'#song-search-input-clear\').removeAttribute(\'disabled\');
        else
          htmx.find(\'#song-search-input-clear\').setAttribute(\'disabled\', \'true\');
        "
      >
      <button id="song-search-input-clear" type="reset" ' . $disabled . '
        hx-on:click="htmx.trigger(\'#song-search-input\', \'clear-search\')"
        >
        <svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" class="bi bi-x-lg" viewBox="0 0 16 16">
          <path d="M2.146 2.854a.5.5 0 1 1 .708-.708L8 7.293l5.146-5.147a.5.5 0 0 1 .708.708L8.707 8l5.147 5.146a.5.5 0 0 1-.708.708L8 8.707l-5.146 5.147a.5.5 0 0 1-.708-.708L7.293 8 2.146 2.854Z"/>
        </svg>
      </button>
      </div>
      <div id="search-indicator" class="htmx-indicator">Searching&hellip;</div>';
}

function helpHints()
{
  echo '<details open>
    <summary>Instructions</summary>
    <p>Enter at least 3 characters (letters/numbers) as a search query.</p>
    <p>You may enter any part of the artist and/or title of the song, and partial words are allowed.</p>
    <p>For example, "pai bla stone" would match "Rolling Stones, The - Paint it black".</p>
    <p>Or <a href="/browse.php" hx-boost="true">browse the songbook by letter</a>.</p>
  </details>';
  if (!okj_accepting())
    echo '<details open>
      <summary>Requests closed</summary>
      <p>You will not be able to request songs at this time.</p>
    </details>';
}

// The API has no lookup-song-by-id route, so artist & title ride along in
// hx-vals to spare the request modal an extra round trip.
function songButtons($songs, $accepting)
{
  foreach ($songs as $song)
  {
    $songId = isset($song['song_id']) ? (int)$song['song_id'] : 0;
    $artist = isset($song['artist']) ? $song['artist'] : '';
    $title = isset($song['title']) ? $song['title'] : '';
    $label = h($artist . ' - ' . $title);

    if (!$accepting || $songId < 1)
    {
      echo "<button class=\"result song\">$label</button>";
      continue;
    }
    $vals = hxvals(array('songid' => $songId, 'artist' => $artist, 'title' => $title));
    echo "<button
      class=\"result song\"
      hx-post=\"/req-modal.php\"
      hx-target=\"body\"
      hx-swap=\"beforeend\"
      hx-vals='$vals'>$label</button>";
  }
}

function reqFormContent($songid = "", $artist = "", $title = "")
{
  $user = okj_user();
  echo "<input type=\"hidden\" name=\"songid\" value=\"" . h($songid) . "\">
  <input type=\"hidden\" name=\"artist\" value=\"" . h($artist) . "\">
  <input type=\"hidden\" name=\"title\" value=\"" . h($title) . "\">";

  if ($user['authenticated'])
  {
    // Signed in: the request is filed under the account, which is what makes
    // it removable and re-orderable later.
    echo "<p>Requesting as <strong>" . h($user['username']) . "</strong></p>
      <div class=\"req-modal-buttons\">
        <button
          id=\"cancel-song-request\"
          type=\"button\"
          class=\"close\"
          hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
          >Close</button>
        <button type=\"submit\" id=\"send-song-request\">Send Request</button>
      </div>";
    return;
  }

  echo "<label>Who will sing it?</label>
  <input type=\"text\" name=\"singer\"
    autocomplete=\"off\" autofocus
    placeholder=\"Enter your name or nickname\"
    hx-on:input=\"
      if (this.value.length > 0)
        htmx.find('#send-song-request').removeAttribute('disabled');
      else
        htmx.find('#send-song-request').setAttribute('disabled', 'true');
    \"
    >
  <p class=\"hint\"><a href=\"/account.php\" hx-boost=\"true\">Sign in</a> to manage your own songs later.</p>
  <div class=\"req-modal-buttons\">
    <button
      id=\"cancel-song-request\"
      type=\"button\"
      class=\"close\"
      hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
      >Close</button>
    <button
      type=\"submit\"
      id=\"send-song-request\"
      disabled
      >Send Request</button>
  </div>";
}

?>
