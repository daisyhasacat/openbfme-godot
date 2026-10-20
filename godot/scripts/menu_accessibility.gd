extends RefCounted
## Shared touch targets and deterministic keyboard/controller focus for native dialogs.
static func prepare(root:Node)->Array[Control]:
 root.set_meta('openbfme_native_menu',true)
 var controls:Array[Control]=[]
 _collect(root,controls)
 for i in controls.size():
  var control:=controls[i]
  var previous:=controls[(i+controls.size()-1)%controls.size()]
  var next:=controls[(i+1)%controls.size()]
  control.focus_previous=control.get_path_to(previous)
  control.focus_next=control.get_path_to(next)
  control.focus_neighbor_top=control.get_path_to(previous)
  control.focus_neighbor_bottom=control.get_path_to(next)
  control.focus_neighbor_left=control.get_path_to(previous)
  control.focus_neighbor_right=control.get_path_to(next)
 return controls

static func _collect(node:Node, controls:Array[Control])->void:
 for child in node.get_children():
  if child is BaseButton or child is LineEdit:
   child.custom_minimum_size.y=maxf(child.custom_minimum_size.y,48)
   child.custom_minimum_size.x=maxf(child.custom_minimum_size.x,48)
   child.add_theme_font_size_override('font_size',18)
   child.focus_mode=Control.FOCUS_ALL
   child.set_meta('openbfme_native_menu',true)
   if child is OptionButton:child.get_popup().add_theme_font_size_override('font_size',20)
   if child.is_visible_in_tree() and not (child is BaseButton and child.disabled):
    controls.append(child)
    if not child.has_meta('scroll_focus'):
     child.set_meta('scroll_focus',true)
     child.focus_entered.connect(func():
      var parent=child.get_parent()
      while parent!=null:
       if parent is ScrollContainer:parent.call_deferred('ensure_control_visible',child);break
       parent=parent.get_parent())
  _collect(child,controls)
