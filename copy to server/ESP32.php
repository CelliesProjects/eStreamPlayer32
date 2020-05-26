<?php

  const trashcan='<svg class="delete_button" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM8 9h8v10H8V9zm7.5-5l-1-1h-5l-1 1H5v2h14V4z"/><path fill="none" d="M0 0h24v24H0V0z"/></svg>';
  const urlicon='<svg class="url_button" xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M19 12v7H5v-7H3v7c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2v-7h-2zm-6 .67l2.59-2.58L17 11.5l-5 5-5-5 1.41-1.41L11 12.67V3h2z"/><path d="M0 0h24v24H0z" fill="none"/></svg>';
  const editicon='<svg class="rename_button" xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/><path d="M0 0h24v24H0z" fill="none"/></svg>';
  const addfolder='<svg class="foldericon addfolder" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M0 0h24v24H0z" fill="none"/><path d="M14 10H2v2h12v-2zm0-4H2v2h12V6zm4 8v-4h-2v4h-4v2h4v4h2v-4h4v-2h-4zM2 16h8v-2H2v2z"/></svg>';
  const emptyicon='<svg class="foldericon" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path /></svg>';
  const saveicon='<svg class="foldericon savebutton" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M0 0h24v24H0z" fill="none"/><path d="M17 3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V7l-4-4zm-5 16c-1.66 0-3-1.34-3-3s1.34-3 3-3 3 1.34 3 3-1.34 3-3 3zm3-10H5V5h10v4z"/></svg>';
  const folderup='<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path fill="none" d="M0 0h24v24H0V0z"/><path d="M11 9l1.42 1.42L8.83 14H18V4h2v12H8.83l3.59 3.58L11 21l-6-6 6-6z"/></svg>';

header('Access-Control-Allow-Origin: *');

if(isset($_GET["folder"])){
  $path=rawurldecode($_GET["folder"]);
  if(strpos($path,"..")!==false)die("No traversing");//no folder traversing
  //don't serve an absolute path but make it relative by removing all leading '/' chars
  $cnt=0;
  while($path[$cnt]==='/')$cnt++;
  $path=substr($path,$cnt);

  if($path!==''){
    $path=$path.'/';
    if(!file_exists($path)){
      header($_SERVER["SERVER_PROTOCOL"]." 404 Not Found",true,404);
      die("Requested resource could not be found.");
    }
    echo '<div id="uplink">'.folderup.'</div>';
  }
  $validFiles="*.{[Mm][Pp]3,[Aa][Aa][Cc]}";
  foreach(glob($path."*",GLOB_ONLYDIR)as$filename){
    echo '<div class="folderlink">';
    $pieces=explode('/',$filename);
    if(glob($filename.'/'.$validFiles,GLOB_BRACE))
      echo addfolder;
    else
      echo emptyicon;
    echo $pieces[count($pieces)-1].'</div>';
  }
  foreach(glob($path.$validFiles,GLOB_BRACE)as$filename){
    $pieces=explode('/',$filename);
    echo '<div class="filelink">'.saveicon.$pieces[count($pieces)-1].'</div>';
  }
  die();
}
?>
Hello!
