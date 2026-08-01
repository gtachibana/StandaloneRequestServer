<?php
include('global.inc');

$songid = isset($_POST['songid']) ? (int)$_POST['songid'] : 0;
$artist = isset($_POST['artist']) ? $_POST['artist'] : '';
$title = isset($_POST['title']) ? $_POST['title'] : '';
$singer = isset($_POST['singer']) ? trim($_POST['singer']) : '';

// Present only for a request that came off the YouTube tab. Re-validated here
// rather than trusted from the form: this is the field that decides which of
// the two request routes runs.
$videoId = isset($_POST['videoid']) ? trim($_POST['videoid']) : '';
if (!preg_match('/^[A-Za-z0-9_-]{11}$/', $videoId)) $videoId = '';
$durationSeconds = isset($_POST['durationSeconds']) ? (int)$_POST['durationSeconds'] : 0;

$user = okj_user();

if ($videoId !== '')
{
  // No guest path at all, unlike a library song: OpenKJ authenticates
  // /local/request/youtube before it looks at anything else, because the
  // download it kicks off has to belong to somebody.
  if (!$user['authenticated'])
  {
    reqFormContent($songid, $artist, $title, $videoId, $durationSeconds);
    die();
  }

  // durationSeconds is the client's number and OpenKJ treats it as such - it is
  // trusted only to refuse an over-long video up front, and overwritten with the
  // real length once the file lands.
  $res = okj_post('/local/request/youtube', array(
    'token' => okj_token(),
    'videoId' => $videoId,
    'title' => $title,
    'artist' => $artist,
    'durationSeconds' => $durationSeconds,
  ));

  if (!$res['ok'])
  {
    // "YouTube requests are not enabled" means the KJ switched the feature off,
    // or yt-dlp stopped working, since capabilities were last cached - so drop
    // that cache and the tab goes away on the next page rather than in 5
    // minutes. Same trick as cheer-action.php.
    if (stripos($res['error'], 'not enabled') !== false
      || stripos($res['error'], 'temporarily unavailable') !== false)
    {
      okj_capabilities_forget();
    }
    echo "<p class=\"error\">" . h($res['error']) . "</p>";
    reqFormContent($songid, $artist, $title, $videoId, $durationSeconds);
    die();
  }

  // The song is queued either way; what differs is whether OpenKJ already had
  // the video on disk. Saying so here is the only warning a singer gets that
  // their turn depends on a download finishing first.
  $ready = (isset($res['data']['media_state']) && $res['data']['media_state'] === 'ready');
  echo "<p>Request sent for " . h($user['username']) . "</p>";
  echo $ready
    ? "<p class=\"hint\">This one is already downloaded and ready to sing.</p>"
    : "<p class=\"hint\">It&rsquo;s downloading now &mdash; the rotation will show when it&rsquo;s ready.</p>";
  echo "<div class=\"req-modal-buttons\">
    <button
      type=\"button\"
      class=\"close\"
      hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
      >Close</button>
    <a class=\"button\" href=\"/queue.php\" hx-boost=\"true\">See the queue</a>
    </div>";
  die();
}

if (!$user['authenticated'] && $singer === '')
{
  reqFormContent($songid, $artist, $title);
  die();
}

if ($user['authenticated'])
{
  // songId must be a JSON number here - the API records request ownership via
  // value("songId").toInt(), and a string would leave the song un-owned.
  $res = okj_post('/local/request', array('token' => okj_token(), 'songId' => $songid));
  $singerLabel = $user['username'];
}
else
{
  $res = okj_post('/api.php', array(
    'command' => 'submitRequest',
    'songId' => $songid,
    'singerName' => $singer,
  ));
  $singerLabel = $singer;
}

if (!$res['ok'])
{
  // OpenKJ rejects duplicates ("already in your queue") and closed queues here,
  // so show the reason and leave the form up to try something else.
  echo "<p class=\"error\">" . h($res['error']) . "</p>";
  reqFormContent($songid, $artist, $title);
  die();
}

echo "<p>Request sent for " . h($singerLabel) . "</p>
  <div class=\"req-modal-buttons\">
  <button
    type=\"button\"
    class=\"close\"
    hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
    >Close</button>
  <a class=\"button\" href=\"/queue.php\" hx-boost=\"true\">See the queue</a>
  </div>";

die();

?>
