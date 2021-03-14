<?php
define(ADDFOLDER_ICON, '<img src="/addfoldericon.svg" class="foldericon addfolder">');
define(EMPTY_ICON, '<img src="/emptyicon.svg" class="foldericon">');
define(UPFOLDER_ICON, '<svg class="icon" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path fill="none" d="M0 0h24v24H0V0z"/><path d="M11 9l1.42 1.42L8.83 14H18V4h2v12H8.83l3.59 3.58L11 21l-6-6 6-6z"/></svg>');
define(AUDIO_ICON, '<svg class="icon" xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0z" fill="none"/><path d="M12 3v9.28c-.47-.17-.97-.28-1.5-.28C8.01 12 6 14.01 6 16.5S8.01 21 10.5 21c2.31 0 4.2-1.75 4.45-4H15V6h4V3h-7z"/></svg>');
define(START_ICON, '<img src="/starticon.svg" class="icon starticon">');

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
    echo '<div id="uplink">'.UPFOLDER_ICON.'</div>';
  }

  $validFiles="*.{[Mm][Pp]3,[Aa][Aa][Cc],[Ww][Aa][Vv],[Mm]3[Uu],[Pp][Ll][Ss],[Aa][Ss][Xx]}";

  foreach(glob($path."*",GLOB_ONLYDIR)as$filename){
    echo '<div class="folderlink">';
    $pieces=explode('/',$filename);
    if(glob($filename.'/'.$validFiles,GLOB_BRACE)) {
      echo ADDFOLDER_ICON.START_ICON;
    }
    else {
      echo EMPTY_ICON.EMPTY_ICON;
    }
    echo '<span class="text">'.$pieces[count($pieces)-1].'</span></div>';
  }
  foreach(glob($path.$validFiles,GLOB_BRACE)as$filename){
    $pieces=explode('/',$filename);
    echo '<div class="filelink">'.AUDIO_ICON.START_ICON.'<span class="text">'.$pieces[count($pieces)-1].'</span></div>';
  }
  die();
}
?>
eStreamPlayer32

