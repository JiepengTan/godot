---
name: Spine模块内置集成
overview: 将 spine_godot 模块从自定义模块路径移动到 Godot 内置 modules 目录，修改构建配置使其作为内置模块自动编译，并通过 scons 命令验证集成成功。
todos:
  - id: copy-module
    content: 复制 spine_godot 目录到 modules/spine_godot
    status: completed
  - id: modify-scsub
    content: 修改 SCsub 文件路径适配内置模块结构
    status: completed
    dependencies:
      - copy-module
  - id: modify-config
    content: 修改 config.py 添加构建开关支持
    status: completed
    dependencies:
      - copy-module
  - id: modify-sconstruct
    content: (可选) 在 SConstruct 添加 spine 全局开关
    status: cancelled
  - id: verify-build
    content: 运行 scons 编译验证集成成功
    status: completed
    dependencies:
      - modify-scsub
      - modify-config
---

# Spine 模块内置集成方案

## 概述

将 `spine-godot/spine_godot` 目录复制到 `modules/spine_godot`，修改构建配置文件使其符合 Godot 内置模块规范，然后通过编译验证集成。

## 目录结构变化

```
godot/
├── modules/
│   ├── spine_godot/          <- 新增目录
│   │   ├── spine-cpp/        <- spine C++ 运行时
│   │   ├── icons/
│   │   ├── docs/
│   │   ├── SCsub             <- 需要修改
│   │   ├── config.py         <- 需要修改
│   │   ├── register_types.cpp
│   │   ├── register_types.h
│   │   └── *.cpp/*.h
│   └── spx/
└── SConstruct                <- 添加 spine 开关（可选）
```

## 实施步骤

### 步骤 1: 复制模块目录

使用 xcopy 命令将 spine_godot 目录复制到 modules 目录：

```powershell
xcopy /E /I "spine-godot\spine_godot" "modules\spine_godot"
```

### 步骤 2: 修改 `modules/spine_godot/SCsub`

原始 [spine-godot/spine_godot/SCsub](spine-godot/spine_godot/SCsub) 使用的路径是为 `custom_modules` 设计的，需要修改为标准模块结构：

**修改后的内容：**

```python
#!/usr/bin/env python
from misc.utility.scons_hints import *

Import("env")
Import("env_modules")

env_spine_godot = env_modules.Clone()

# Spine C++ 运行时的头文件路径
env_spine_godot.Append(CPPPATH=["spine-cpp/include"])

# 如果是生成 VS 项目，也需要添加到主 env
if env["vsproj"]:
    env.Append(CPPPATH=["#modules/spine_godot/spine-cpp/include"])

# 编译 spine-cpp 源文件
env_spine_godot.add_source_files(env.modules_sources, "spine-cpp/src/spine/*.cpp")

# 编译 spine_godot 绑定文件
env_spine_godot.add_source_files(env.modules_sources, "*.cpp")

# Clang 下禁用 override 警告
if not env_spine_godot.msvc:
    env_spine_godot.Append(CXXFLAGS=["-Wno-inconsistent-missing-override"])
```

**关键修改点：**

- 添加 `Import("env_modules")` 并使用 `env_modules.Clone()`
- 路径从 `#../spine_godot/spine-cpp/include` 改为 `spine-cpp/include`
- vsproj 路径改为 `#modules/spine_godot/spine-cpp/include`

### 步骤 3: 修改 `modules/spine_godot/config.py`

原始 [spine-godot/spine_godot/config.py](spine-godot/spine_godot/config.py) 保持基本不变，但可以添加开关支持：

**修改后的内容：**

```python
def can_build(env, platform):
    return env.get("module_spine_godot_enabled", True)

def configure(env):
    pass

def get_doc_path():
    return "docs"

def get_doc_classes():
    return [
        "SpineAnimation",
        "SpineAnimationState",
        "SpineAnimationTrack",
        "SpineAtlasResource",
        "SpineAttachment",
        "SpineBone",
        "SpineBoneData",
        "SpineBoneNode",
        "SpineConstraintData",
        "SpineEvent",
        "SpineIkConstraint",
        "SpineIkConstraintData",
        "SpinePathConstraint",
        "SpinePathConstraintData",
        "SpineSkeleton",
        "SpineSkeletonDataResource",
        "SpineSkeletonFileResource",
        "SpineSkin",
        "SpineSlot",
        "SpineSlotData",
        "SpineSlotNode",
        "SpineSprite",
        "SpineTimeline",
        "SpineTrackEntry",
        "SpineTransformConstraint",
        "SpineTransformConstraintData"
    ]
```

### 步骤 4: (可选) 修改 SConstruct 添加全局开关

在 [SConstruct](SConstruct) 第 270 行附近，`spx` 选项之后添加：

```python
opts.Add(BoolVariable("spx", "Enable the spx library", True))
opts.Add(BoolVariable("spine", "Enable the Spine runtime integration", True))  # 新增
```

然后在第 1012 行附近添加：

```python
if env["spx"]:
    env.Append(CPPDEFINES=["SPX_ENABLED"])
if env.get("spine", True):
    env.Append(CPPDEFINES=["SPINE_ENABLED"])  # 新增
```

并修改 config.py 中的 `can_build`：

```python
def can_build(env, platform):
    return env.get("spine", True)
```

### 步骤 5: 编译验证

```powershell
# 在 godot 目录下执行，启用 spine 模块编译
scons platform=windows target=template_release arch=x86_64 module_spine_godot_enabled=yes -j8

# 或者如果添加了 SConstruct 全局开关
scons platform=windows target=template_release arch=x86_64 spine=yes -j8
```

### 验证集成成功的标志

1. 编译过程中应该看到 spine 相关的 `.cpp` 文件被编译
2. 编译输出中不应有与 spine 模块相关的错误
3. 最终生成的可执行文件应包含 Spine 类

## 注意事项

- spine_godot 的 `register_types.cpp` 已经兼容 Godot 4 内置模块格式，无需修改
- spine-cpp 是 Spine 的 C++ 运行时库，必须一起复制
- 如果后续需要更新 spine 版本，需要手动同步 spine-godot 目录的更新