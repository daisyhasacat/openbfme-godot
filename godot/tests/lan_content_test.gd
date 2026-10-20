extends SceneTree
func _initialize():call_deferred("run")
func run():
 var saved_settings:={}
 for path in ["user://mods.cfg","user://lan-content.cfg"]:
  if FileAccess.file_exists(path):saved_settings[path]=FileAccess.get_file_as_bytes(path)
 var script=load("res://scripts/lan_content.gd")
 assert(script!=null)
 assert(script.safe_path("art/textures/sample.dds"))
 for path in ["../secret","/absolute","C:/secret","art/../../secret","art\\secret","run.exe"]:
  assert(not script.safe_path(path))
 assert(script.valid_id("a".repeat(64)))
 assert(not script.valid_id("../pack"))
 var client=script.new();root.add_child(client)
 var malicious:="user://lan-unsafe-fixture.zip"
 var zip:=ZIPPacker.new();assert(zip.open(malicious)==OK)
 assert(zip.start_file("../escaped.txt")==OK);zip.write_file("synthetic".to_utf8_buffer());zip.close_file();zip.close()
 var rejected:String=await client._install({"id":"b".repeat(64),"kind":"mod"},malicious)
 assert(rejected.begins_with("Unsafe or duplicate path"),rejected)
 assert(not FileAccess.file_exists("user://escaped.txt"))
 DirAccess.remove_absolute(malicious)
 if not client.staging_path.is_empty():script._remove_tree(client.staging_path)
 client.staging_path=""
 var url=OS.get_environment("OPENBFME_LAN_TEST_URL")
 if not url.is_empty():
  # Synthetic stored ZIP: Wrapper/art/textures/test.txt, no game assets.
  var nested_zip="user://lan-nested-fixture.zip"
  var nested_file=FileAccess.open(nested_zip,FileAccess.WRITE)
  nested_file.store_buffer(Marshalls.base64_to_raw("UEsDBBQAAAAAALB7VF3GzBlcGAAAABgAAAAdAAAAV3JhcHBlci9hcnQvdGV4dHVyZXMvdGVzdC50eHRuZXN0ZWQgc3ludGhldGljIGZpeHR1cmVQSwECFAMUAAAAAACwe1RdxswZXBgAAAAYAAAAHQAAAAAAAAAAAAAAgAEAAAAAV3JhcHBlci9hcnQvdGV4dHVyZXMvdGVzdC50eHRQSwUGAAAAAAEAAQBLAAAAUwAAAAAA"));nested_file.close()
  var nested_id="c".repeat(64)
  var nested_root="user://mods/lan-"+nested_id
  script._remove_tree(nested_root)
  var problem:String=await client._install({"id":nested_id,"kind":"mod","textures_only":true},nested_zip)
  assert(problem.is_empty(),problem)
  var nested_config:=ConfigFile.new();assert(nested_config.load("user://mods.cfg")==OK)
  var nested_path=ProjectSettings.globalize_path(nested_root.path_join("Wrapper"))
  assert(Array(nested_config.get_value("mods","order",[])).has(nested_path))
  assert(Array(nested_config.get_value("mods","textures_only",[])).has(nested_path))
  script._remove_tree(nested_root)
  DirAccess.remove_absolute(nested_zip)
  client.address.text=url
  await client._connect_server()
  assert(client.rows.get_child_count()==1,client.status.text)
  var catalog:Dictionary=client._json_reply(await client._http_get("/catalog.json"))
  var fixture_target="user://mods/lan-"+str(catalog.packs[0].id)
  script._remove_tree(fixture_target)
  await client._download(catalog.packs[0])
  assert(client.status.text.begins_with("Installed "),client.status.text)
  var target="user://mods/lan-"+str(catalog.packs[0].id)
  assert(FileAccess.get_file_as_string(target.path_join("art/textures/fixture.txt"))=="synthetic texture fixture")
  var config=ConfigFile.new();assert(config.load("user://mods.cfg")==OK)
  var absolute=ProjectSettings.globalize_path(target)
  assert(Array(config.get_value("mods","textures_only",[])).has(absolute))
  assert(not Array(config.get_value("mods","enabled",[])).has(absolute))
  script._remove_tree(target)
  DirAccess.remove_absolute("user://mods.cfg")
  DirAccess.remove_absolute("user://lan-content.cfg")
  for path in saved_settings:
   var restore=FileAccess.open(path,FileAccess.WRITE);restore.store_buffer(saved_settings[path]);restore.close()
 client.free()
 print("LAN CONTENT PASS")
 quit()
