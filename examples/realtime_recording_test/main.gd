extends Control

@onready var record_button: Button = $VBoxContainer/RecordButton
@onready var status_label: Label = $VBoxContainer/StatusLabel
@onready var audio_player: AudioStreamPlayer = $AudioStreamPlayer
@onready var progress_bar: ProgressBar = $VBoxContainer/ProgressBar

var recording = false
var record_time = 0.0
var max_record_time = 10.0  # 最大录制10秒
var audio_generator_playback: AudioStreamGeneratorPlayback

func _ready():
	# 设置UI
	record_button.text = "开始录制"
	status_label.text = "准备就绪 - 实时录制模式"
	progress_bar.value = 0
	progress_bar.max_value = max_record_time
	
	# 连接信号
	record_button.pressed.connect(_on_record_button_pressed)
	
	

func _process(delta):
	if recording:
		record_time += delta
		progress_bar.value = record_time
		status_label.text = "录制中... %.1f/%.1f 秒" % [record_time, max_record_time]
		
		# 自动停止录制
		if record_time >= max_record_time:
			_stop_recording()
	

func _on_record_button_pressed():
	if not recording:
		_start_recording()
	else:
		_stop_recording()

func _start_recording():
	recording = true
	record_time = 0.0
	record_button.text = "停止录制"
	status_label.text = "开始录制..."
	

func _stop_recording():
	recording = false
	record_button.text = "开始录制"


func _on_play_audio_pressed():
	# 音频生成器一直在运行，这里不需要特别操作
	print("测试音频播放")

func _on_stop_audio_pressed():
	# 暂时停止音频生成（通过停止推送新的缓冲区）
	print("停止音频") 
