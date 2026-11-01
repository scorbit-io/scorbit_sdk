@tool
extends EditorPlugin

# Enabling the plugin registers the Scorbit autoload, so game code can call Scorbit.* anywhere.

const AUTOLOAD_NAME := "Scorbit"


func _enable_plugin() -> void:
	add_autoload_singleton(AUTOLOAD_NAME, "res://addons/scorbit/scorbit.gd")


func _disable_plugin() -> void:
	remove_autoload_singleton(AUTOLOAD_NAME)
