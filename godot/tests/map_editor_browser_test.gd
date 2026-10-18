extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var editor=load('res://scripts/map_editor.gd').new();root.add_child(editor)
 editor.object_records={
  'Rock':{'cost':'0','flags':['scenery'],'notes':'environment rock'},
  'Barracks':{'cost':'300','flags':['building','builderBuildable'],'notes':'builder command'},
  'Plot':{'cost':'0','flags':['building','plot'],'notes':'foundation'},
  'Placeholder':{'cost':'0','flags':['building','placeholder'],'notes':'helper'},
  'Unfinished':{'cost':'0','flags':['building','unfinishedContent'],'notes':'obsolete'},
  'Unsupported':{'cost':'0','flags':['building','unsupported'],'notes':'unsupported behavior'}}
 editor.catalog=PackedStringArray(editor.object_records.keys());editor._filter('')
 assert(editor.filtered==PackedStringArray(['Rock','Barracks','Unsupported']))
 assert(editor.object_grid.columns==3)
 assert(editor.object_grid.get_child_count()==3)
 for filter in editor.filter_toggles.values():filter.button_pressed=false
 editor._filter('');assert(editor.filtered.size()==6)
 editor.object_category.select(1);editor._filter('');assert(editor.filtered.size()==5)
 editor.object_category.select(0);editor._filter('environment');assert(editor.filtered==PackedStringArray(['Rock']))
 editor._select_object('Rock');assert(editor._selected()=='Rock')
 editor.grid_mode=false;editor._render_object_results()
 var row=editor.object_tree.get_root().get_first_child()
 assert(row.get_text(0)=='Rock' and row.get_text(1)=='Scenery / other' and row.get_text(2)=='0')
 assert(row.get_metadata(0)=='Rock')
 editor._filter('no such object');assert(editor.filtered.is_empty() and editor._selected().is_empty())
 editor.object_category.select(0);editor.availability.select(1);editor._filter('')
 assert(editor.filtered==PackedStringArray(['Barracks']),'Buildable only includes constructed buildings')
 editor.object_records.Barracks.faction='Mordor';editor._filter('mordor')
 assert(editor.filtered==PackedStringArray(['Barracks']),'Faction search includes faction metadata')
 editor.object_records.Barracks.flags=['unit','trainable'];editor._filter('mordor')
 assert(editor.filtered==PackedStringArray(['Barracks']),'Buildable includes trained units')
 var details=editor._object_details({'id':42,'template':'Barracks','owner':'Tester','player_id':1,'team':'Team 1','health':50,'max_health':100,'x':10,'y':20,'z':0,'angle':0,'ai_state':'Idle'})
 assert('Object ID: 42' in details and 'Health: 50 / 100' in details and 'Faction: Mordor' in details and 'AI state: Idle' in details,'Inspector identity and useful state')
 editor.free();print('MAP EDITOR BROWSER PASS');quit()
