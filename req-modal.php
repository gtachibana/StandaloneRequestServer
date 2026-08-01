<?php
include('global.inc');

// Artist, title & length come in from the result button rather than a lookup:
// the Local Mode API has no fetch-song-by-id route. The length is display only
// and stops here - req-send.php has no use for it.
$songid = isset($_POST['songid']) ? (int)$_POST['songid'] : 0;
$artist = isset($_POST['artist']) ? $_POST['artist'] : '';
$title = isset($_POST['title']) ? $_POST['title'] : '';
$duration = isset($_POST['duration']) ? okj_fmt_duration($_POST['duration']) : '';

// Set when the tap came from the YouTube tab rather than the songbook. Validated
// to the same shape OpenKJ accepts, so a malformed id never reaches the form as
// a hidden field that only fails at send time.
$videoId = isset($_POST['videoid']) ? trim($_POST['videoid']) : '';
if (!preg_match('/^[A-Za-z0-9_-]{11}$/', $videoId)) $videoId = '';
$durationSeconds = isset($_POST['duration']) ? (int)$_POST['duration'] : 0;

echo "<div
    id=\"req-modal\"
    hx-on:remove-req-modal=\"
      this.addEventListener('animationend', function () { this.remove(); });
      htmx.addClass(this, 'closing');
    \"
    >
  <div
    class=\"req-modal-underlay\"
    hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
    ></div>
  <div id=\"req-song-content\" class=\"req-modal-content\">
    <h2>" . ($videoId !== '' ? 'Requesting from YouTube' : 'Requesting Song') . "</h2>
    <p class=\"song\">" . h($artist . ' - ' . $title) . "</p>"
  . ($duration !== '' ? "<p class=\"hint song-meta\">$duration</p>" : '') . "
    <form
      id=\"req-modal-form\"
      hx-post=\"req-send.php\"
      >";
reqFormContent($songid, $artist, $title, $videoId, $durationSeconds);
echo "</form>
  </div>
</div>";

die();

?>
