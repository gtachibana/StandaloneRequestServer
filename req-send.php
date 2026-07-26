<?php
include('global.inc');

$songid = isset($_POST['songid']) ? (int)$_POST['songid'] : 0;
$artist = isset($_POST['artist']) ? $_POST['artist'] : '';
$title = isset($_POST['title']) ? $_POST['title'] : '';
$singer = isset($_POST['singer']) ? trim($_POST['singer']) : '';

$user = okj_user();

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
