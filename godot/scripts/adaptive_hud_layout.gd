extends RefCounted
## The shipped JSON is the schema/defaults; exported games get an editable user copy.
const DEFAULT_PATH='res://data/adaptive_hud_layout.json'
const USER_PATH='user://adaptive-hud-layout.json'
var path:=DEFAULT_PATH
var values:Dictionary={}
var last_error:=''
var _defaults:Dictionary={}
var _last_text:=''

func initialize()->bool:
 var json=JSON.new()
 if json.parse(FileAccess.get_file_as_string(DEFAULT_PATH))!=OK or not json.data is Dictionary:
  push_error('Cannot read shipped HUD layout JSON');return false
 values=json.data.duplicate(true)
 _defaults=values.duplicate(true)
 if OS.has_feature('template'):
  path=USER_PATH
  if not FileAccess.file_exists(path):
   var file=FileAccess.open(path,FileAccess.WRITE)
   if file==null:push_error('Cannot create HUD layout: '+path);return false
   file.store_string(JSON.stringify(values,'  ')+'\n');file.close()
 reload(true)
 return true

func reload(force:=false)->bool:
 if not FileAccess.file_exists(path):
  last_error='HUD layout file not found: '+path
  if force:push_error(last_error)
  return false
 var text=FileAccess.get_file_as_string(path)
 if not force and text==_last_text:return false
 _last_text=text
 var json=JSON.new()
 if json.parse(text)!=OK:
  last_error='HUD layout JSON: '+json.get_error_message();push_error(last_error);return false
 if not json.data is Dictionary:
  last_error='HUD layout must be a JSON object';push_error(last_error);return false
 var next=_defaults.duplicate(true)
 for key in json.data:
  var value=json.data[key]
  if not values.has(key) or not (value is float or value is int) or not is_finite(float(value)) or float(value)<=0:
   last_error='Invalid HUD sizing value: '+str(key);push_error(last_error);return false
  if (key.ends_with('columns') or key=='font_size') and (value!=floor(value) or value>32):
   last_error='HUD columns/font size must be an integer from 1 to 32: '+str(key);push_error(last_error);return false
  if key.ends_with('fraction') and value>1:
   last_error='HUD layout fraction must be at most 1: '+str(key);push_error(last_error);return false
  next[key]=value
 if next.minimum_scale>next.maximum_scale:
  last_error='HUD minimum_scale exceeds maximum_scale';push_error(last_error);return false
 values=next;last_error=''
 return true
