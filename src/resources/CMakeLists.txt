# QtResources (the resource-string accessor) moved down to src/lib_core/utility so `lib` -- which reaches for it
# from NodeType/GraphViewStyle -- no longer depends up on this target. res_gui now only carries the compiled
# Qt .qrc data (added by qt_add_resources below); QtResources reads that data at runtime via QFile.
add_library(Sourcetrail_res_gui)

qt_add_resources(Sourcetrail_res_gui Sourcetrail_resources
	PREFIX "/"
	FILES
		# Before changing any paths, check whether they are referenced via <setting:gui_path> in the stylesheets!
		about/about.css
		about/logo_sourcetrail.png
		
		bookmark_view/images/arrow_down.png
		bookmark_view/images/arrow_line_down.png
		bookmark_view/images/arrow_line_up.png
		bookmark_view/images/arrow_right.png
		bookmark_view/images/bookmark_active.svg
		bookmark_view/images/bookmark_delete_icon.png
		bookmark_view/images/bookmark_edit_icon.png
		bookmark_view/images/bookmark_list_icon.svg
		bookmark_view/images/edit_bookmark_icon.svg
		bookmark_view/bookmark_view.css
		
		code_view/code_view.css
		code_view/images/arrow_left.png
		code_view/images/arrow_right.png
		code_view/images/edit.png
		code_view/images/pattern_bright.png
		code_view/images/pattern_dark.png
		code_view/images/pattern_grey.png
		
		custom_trail_view/custom_trail_view.css
		custom_trail_view/images/circle.png
		
		graph_view/images/annotation.png
		graph_view/images/arrow.png
		graph_view/images/bundle.png
		graph_view/images/default.png
		graph_view/images/enum.png
		graph_view/images/file_incomplete.png
		graph_view/images/file.svg
		graph_view/images/graph_arrow.svg
		graph_view/images/graph_custom.svg
		graph_view/images/graph_down.svg
		graph_view/images/graph_left.svg
		graph_view/images/graph.svg
		graph_view/images/graph_right.svg
		graph_view/images/graph_up.svg
		graph_view/images/group_language.svg
		graph_view/images/group_namespace.svg
		graph_view/images/legend.svg
		graph_view/images/macro.png
		graph_view/images/namespace.png
		graph_view/images/pattern.png
		graph_view/images/private.png
		graph_view/images/protected.png
		graph_view/images/public.png
		graph_view/images/template.png
		graph_view/images/typedef.png
		graph_view/images/zoom_in.svg
		graph_view/images/zoom_out.svg
		graph_view/graph_view.css
		
		glyphs/add.svg
		glyphs/auto_hide.svg
		glyphs/chevron_down.svg
		glyphs/chevron_right.svg
		glyphs/chevron_up.svg
		glyphs/close.svg
		glyphs/edit.svg
		glyphs/file_incomplete.svg
		glyphs/float.svg
		glyphs/maximize.svg
		glyphs/minimize.svg
		glyphs/snippets.svg
		glyphs/unauto_hide.svg
		glyphs/unfold.svg
		
		history_list/history_list.css
		history_list/images/arrow.png
		
		icon/cdb_icon.png
		icon/cmake_icon.png
		icon/c_icon.png
		icon/cpp_icon.png
		icon/empty_icon.png
		icon/logo_1024_1024.png
		icon/project_256_256.png
		icon/project.ico
		
		indexing_dialog/indexing_dialog.css
		indexing_dialog/error.png
		indexing_dialog/flag.png
		indexing_dialog/progress_bar_element.png
		
		keyboard_shortcuts/keyboard_shortcuts.css
		
		main/main.css
		main/scrollbar.css
		
		refresh_view/refresh_view.css
		refresh_view/images/refresh.svg
		
		screen_search_view/images/close.png
		screen_search_view/screen_search_view.css
		
		search_view/images/arrow.png
		search_view/images/home.svg
		search_view/images/search.svg
		search_view/search_view.css
		
		startscreen/delete_icon.png
		startscreen/github_icon.png
		startscreen/startscreen.css
		
		statusbar_view/dot.png
		statusbar_view/loader.gif
		
		tabbed_view/arrow_down.png
		tabbed_view/arrow_up.png
		tabbed_view/tabbed_view.css
		
		tabs_view/tabs_view.css
		
		tooltip_view/tooltip_view.css
		
		undoredo_view/images/arrow_left.svg
		undoredo_view/images/arrow_right.svg
		undoredo_view/images/history.svg
		undoredo_view/undoredo_view.css
		
		window/dots.png
		window/dots_hover.png
		window/help_hover.png
		window/help.png
		window/listbox.css
		window/logo.png
		window/minus_hover.png
		window/minus.png
		window/plus_hover.png
		window/plus.png
		window/refresh_hover.png
		window/refresh.png
		window/size_grip_black.png
		window/size_grip_white.png
		window/source_group_add_hover.png
		window/source_group_add.png
		window/source_group_copy_hover.png
		window/source_group_copy.png
		window/source_group_delete_hover.png
		window/source_group_delete.png
		window/window.css
)

setDefaultTargetOptions(Sourcetrail_res_gui)

set_target_properties(Sourcetrail_res_gui
	PROPERTIES
		AUTOMOC ON
)

target_include_directories(Sourcetrail_res_gui
	PUBLIC
		.
)

target_link_libraries(Sourcetrail_res_gui
	PUBLIC
		Sourcetrail_lib_core
		External_lib_qt
)
