<?php
include('global.inc');
include('queue-render.inc');

// Every action here is scoped to the signed-in user's own songs; the API
// re-checks ownership, this is just to avoid pointless round trips.
$user = okj_user();
$action = isset($_POST['action']) ? $_POST['action'] : '';
$entryId = isset($_POST['entryId']) ? (string)$_POST['entryId'] : '';
$error = '';

if (!$user['authenticated'])
{
  $error = 'Please sign in first.';
}
elseif ($action === 'away')
{
  $away = (isset($_POST['away']) && $_POST['away'] === '1');
  $res = okj_post('/local/user/away', array('token' => okj_token(), 'away' => $away));
  if (!$res['ok']) $error = $res['error'];
}
elseif ($action === 'remove')
{
  $res = okj_post('/local/request/remove', array('token' => okj_token(), 'entryId' => $entryId));
  if (!$res['ok']) $error = $res['error'];
}
elseif ($action === 'up' || $action === 'down')
{
  $res = okj_post('/local/request/move', array(
    'token' => okj_token(),
    'entryId' => $entryId,
    'direction' => $action,
  ));
  if (!$res['ok']) $error = $res['error'];
}
else
{
  $error = 'Unknown action.';
}

// okj_user() memoized the pre-action state; the away flag it holds is now stale.
if ($action === 'away' && $error === '') okj_user_refresh();

renderQueueFragment($error);

?>
