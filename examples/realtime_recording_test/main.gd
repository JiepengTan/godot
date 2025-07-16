extends Control

@onready var ball: ColorRect = $Ball
@onready var title_label: Label = $TitleLabel
@onready var audio_player: AudioStreamPlayer = $AudioStreamPlayer

var time_passed = 0.0
var jump_height = 100.0  # 跳动高度
var jump_speed = 3.0     # 跳动速度
var original_y: float    # 小球的原始Y位置

func _ready():
	# 设置标题
	title_label.text = "跳动的小球"
	
	# 记录小球的原始位置
	original_y = ball.position.y
	
	# 确保背景音乐正在播放
	if not audio_player.playing:
		audio_player.play()
	
	print("小球跳动demo已启动，背景音乐播放中")

func _process(delta):
	time_passed += delta * 2
	
	# 使用sin函数创建跳动效果
	var jump_offset = sin(time_passed * jump_speed) * jump_height
	
	
	# 更新小球位置
	ball.position.y = original_y - jump_offset
	
	# 可选：改变小球颜色以增加视觉效果
	var color_intensity = 0.5 + 0.5 * sin(time_passed * jump_speed * 2)
	ball.color = Color(1.0, color_intensity * 0.5, color_intensity * 0.5, 1.0) 
