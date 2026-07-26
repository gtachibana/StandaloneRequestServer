<?php
include('global.inc');
include('queue-render.inc');

$fragment = okj_is_fragment();

if (!$fragment)
{
  siteheader();
  navbar('queue');
  echo '<h2 class="page-title">Tonight&rsquo;s rotation</h2>';
}

renderQueueFragment();

if (!$fragment) sitefooter();

?>
