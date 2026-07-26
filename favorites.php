<?php
include('global.inc');
include('favorites-render.inc');

$fragment = okj_is_fragment();

if (!$fragment)
{
  siteheader();
  navbar('favorites');
  echo '<h2 class="page-title">Your favorites</h2>';
}

renderFavoritesFragment();

if (!$fragment) sitefooter();

?>
