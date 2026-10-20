extends SceneTree
class OptionsShell extends Node2D:
 var spec:Dictionary
 func set_openbfme_available(_value):pass
 func openbfme_page_show(value):spec=value;return {'ok':true}
func _initialize():call_deferred('run')
func run():
 var helper=load('res://scripts/menu_accessibility.gd')
 var client=load('res://scripts/lan_content.gd').new();root.add_child(client)
 await process_frame
 var controls=helper.prepare(client)
 assert(not controls.is_empty())
 for control in controls:
  assert(control.custom_minimum_size.y==48)
  assert(control.focus_mode==Control.FOCUS_ALL)
  assert(not control.focus_next.is_empty())
 var focused=root.gui_get_focus_owner()
 assert(focused is Button and focused.text=='Connect')
 var controller=load('res://scripts/controller_input.gd').new();root.add_child(controller)
 assert(controller._native_menu_focus())
 var options=load('res://scripts/openbfme_options_screen.gd').new()
 options._pending=load('res://scripts/display_settings.gd').new()
 var shell=OptionsShell.new();root.add_child(shell);options.shell=shell;root.add_child(options);options.set_process(false)
 options._open=true;options._show_tab('Mods');await process_frame
 assert('Mods' in shell.spec.tabs)
 assert(options._mods_button.visible)
 assert(options._mods_button.custom_minimum_size==Vector2(360,64))
 options._mods_button.pressed.emit();await process_frame
 var manager=root.get_node('ModManagerLayer')
 assert(manager!=null)
 assert(manager._game_picker.has_focus())
 for control in helper.prepare(manager):assert(control.custom_minimum_size.y==48)
 var dynamic_button:=Button.new();dynamic_button.text='Dynamic dialog control';manager.add_child(dynamic_button);dynamic_button.grab_focus()
 assert(controller._native_menu_focus(),'Later dialog controls inherit native routing')
 dynamic_button.queue_free()
 manager._close();await process_frame
 options._mods_button.grab_focus()
 var back:=InputEventAction.new();back.action='ui_cancel';back.pressed=true
 options._input(back)
 assert(options.current_tab()=='Display','Controller Back leaves native Mods tab')
 assert(not options._mods_button.visible)
 options.free();shell.free();controller.free();client.free()
 print('MENU ACCESSIBILITY PASS: targets, focus, controller routing, Options Mods entry')
 quit()
