<?php
include("global.inc");

siteheader();
navbar('songbook');

searchform();
echo '<div id="data-target">';
helpHints();
echo '</div>';

sitefooter();

?>
