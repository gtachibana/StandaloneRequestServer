<?php
include('global.inc');

// Artist & title come in from the result button rather than a lookup: the
// Local Mode API has no fetch-song-by-id route.
$songid = isset($_POST['songid']) ? (int)$_POST['songid'] : 0;
$artist = isset($_POST['artist']) ? $_POST['artist'] : '';
$title = isset($_POST['title']) ? $_POST['title'] : '';

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
    <h2>Requesting Song</h2>
    <p class=\"song\">" . h($artist . ' - ' . $title) . "</p>
    <form
      id=\"req-modal-form\"
      hx-post=\"req-send.php\"
      >";
reqFormContent($songid, $artist, $title);
echo "</form>
  </div>
</div>";

die();

?>
