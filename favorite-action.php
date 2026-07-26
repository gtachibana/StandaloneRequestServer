<?php
include('global.inc');
include('favorites-render.inc');

// Favorites are per-account; the API re-checks the token, this is just to keep
// a signed-out tap from becoming a pointless round trip.
$user = okj_user();
$action = isset($_POST['action']) ? $_POST['action'] : '';
$songId = isset($_POST['songid']) ? (int)$_POST['songid'] : 0;
$render = (isset($_POST['render']) && $_POST['render'] === 'list') ? 'list' : 'toggle';
$error = '';

// What the tap is asking for; corrected below if the call doesn't take.
$isFavorite = ($action === 'add');

if (!$user['authenticated'])
{
  $error = 'Please sign in first.';
}
elseif ($songId < 1)
{
  $error = 'Unknown song.';
}
elseif ($action === 'add' || $action === 'remove')
{
  // songId goes over the wire as a JSON *number*: the API reads it with
  // value("songId").toInt(), which yields 0 for a string.
  $path = $isFavorite ? '/local/user/favorites' : '/local/user/favorites/remove';
  $res = okj_post($path, array('token' => okj_token(), 'songId' => $songId));
  if (!$res['ok']) $error = $res['error'];
  else okj_favorites_note($songId, $isFavorite);
}
else
{
  $error = 'Unknown action.';
}

if ($render === 'list')
{
  renderFavoritesFragment($error);
}
else
{
  // Nothing moved, so the star has to go back to what it was.
  if ($error !== '') $isFavorite = !$isFavorite;
  favStar($songId, $isFavorite, $error);
}

?>
