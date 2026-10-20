extends SceneTree
## Mounted-file extraction / validated installation for tools/maps/merge_maps.py.
func _initialize(): call_deferred('_run')
func _run():
 var args=OS.get_cmdline_user_args()
 if args.size()!=1:push_error('Expected map merge request JSON');quit(1);return
 var request=JSON.parse_string(FileAccess.get_file_as_string(args[0]))
 var fs=ClassDB.instantiate('RetailFileSystem')
 var mounted:Dictionary=fs.mount_retail()
 var result:Dictionary=mounted
 if mounted.get('ok',false):
  if request.mode=='extract':
   var data:PackedByteArray=fs.read_file(request.key)
   result={'ok':not data.is_empty(),'archive':fs.get_archive_for_file(request.key),'key':request.key}
   for companion in ['map.ini','map.str']:
    if fs.file_exists(str(request.key).get_base_dir().path_join(companion)):
     result={'ok':false,'error':'Source companion '+companion+' needs explicit merging'}
   if result.ok:
    var file=FileAccess.open(request.path,FileAccess.WRITE)
    if file==null:result={'ok':false,'error':error_string(FileAccess.get_open_error())}
    else:file.store_buffer(data)
  elif request.mode=='install':result=fs.install_library_map(request.path,'user://installed-maps')
  else:result=fs.inspect_library_map(request.path,false)
 var output=FileAccess.open(request.output,FileAccess.WRITE)
 if output==null:push_error('Cannot write merge result');quit(1);return
 output.store_string(JSON.stringify(result,'\t'));output.close()
 quit(0 if result.get('ok',false) else 1)
