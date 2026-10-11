# 编码规范

## 1. 命名规则

### 1.1 函数命名
- **函数名**：使用驼峰命名法，首字母小写
- **示例**：`void processData()`、`int calculateSum()`

### 1.2 变量命名
- **局部变量**：使用驼峰命名法，首字母小写
- **成员变量**：使用驼峰命名法，首字母小写，后缀加下划线
- **示例**：`int count`、`std::string name_`

### 1.3 类命名
- **类名**：使用驼峰命名法，首字母大写
- **示例**：`class NetworkConfig`、`class ConfigManager`

### 1.4 命名空间
- **命名空间**：使用小写字母，多个单词用下划线连接
- **示例**：`namespace cncpp`、`namespace network_lib`

### 1.5 常量命名
- **常量**（`const` / `constexpr` / 类内静态常量）：`k` 前缀 + 驼峰命名法，首字母大写
- **示例**：`constexpr int kMaxBufferSize = 1024`、`const int kDefaultPort = 8080`
- **宏**：全大写，多个单词用下划线连接；该风格**只用于宏**，且应优先用常量替代宏
- **示例**：`#define LOG_BUFFER_SIZE 4096`

### 1.6 枚举命名
- **枚举类型名**：与类名一致（驼峰，首字母大写），优先使用 `enum class`
- **枚举值**：与常量一致（`k` 前缀 + 驼峰）
- **示例**：
  ```cpp
  enum class PayloadFormat
  {
      kRawStruct,
      kProtobuf,
  };
  ```

## 2. 代码风格

基线为 **Google 风格**（`.clang-format` 中 `BasedOnStyle: Google`），本章列出的是项目在 Google 之上的定制项；未在 `.clang-format` 中声明的选项一律遵循 Google 默认值。

### 2.1 缩进
- **缩进宽度**：4 个空格
- **禁止使用**：Tab 缩进
- **续行缩进**：4 个空格

### 2.2 大括号
- **风格**：Allman 风格（全换行）
- **示例**：
  ```cpp
  if (condition)
  {
      // 代码
  }
  ```

### 2.3 空格
- **运算符**：运算符前后加空格
- **括号**：括号内不加空格
- **函数调用**：函数名与括号之间不加空格
- **示例**：
  ```cpp
  int result = a + b;
  if (result > 0)
  {
      processData(result);
  }
  ```

### 2.4 行宽
- **最大行宽**：120 列
- **超过行宽**：适当换行，保持可读性

### 2.5 指针和引用
- **风格**：指针/引用符号紧贴类型
- **示例**：`std::string* ptr`、`const std::string& ref`

### 2.6 其他约定
- **对齐**：连续赋值、连续声明按列对齐（`AlignConsecutive*`）
- **短代码块**：禁止函数/if/循环/lambda/代码块合并到一行
- **include**：不重排分组（`IncludeBlocks: Preserve`），仅组内排序
- **命名空间**：namespace 内部代码整体缩进（`NamespaceIndentation: All`）

## 3. 注释规范

### 3.1 文件头部注释
- **内容**：文件描述、作者、创建日期、修改历史
- **示例**：
  ```cpp
  /**
   * @file config.h
   * @brief 配置管理模块
   * @author [Author Name]
   * @date 2026-04-12
   */
  ```

### 3.2 函数注释
- **内容**：函数功能、参数说明、返回值说明
- **示例**：
  ```cpp
  /**
   * @brief 从配置文件读取配置
   * @param file_path 配置文件路径
   * @return 是否读取成功
   */
  bool ReadFromFile(const std::string& file_path);
  ```

### 3.3 行内注释
- **使用场景**：复杂逻辑、关键算法、特殊处理
- **风格**：简洁明了，解释代码意图
- **示例**：
  ```cpp
  // 计算平均值
  double average = sum / count;
  ```

## 4. 代码结构

### 4.1 文件组织
- **头文件**：`.h` 后缀，包含类和函数声明
- **源文件**：`.cpp` 后缀，包含函数实现
- **命名**：文件名与类名保持一致

### 4.2 模块划分
- **功能模块**：按功能划分模块，每个模块有明确的职责
- **文件结构**：
  ```
  project/
  ├── src/
  │   ├── base/
  │   │   ├── config.h
  │   │   ├── network.h
  │   │   └── ...
  │   └── examples/
  │       ├── network_example.cpp
  │       └── ...
  └── CMakeLists.txt
  ```

### 4.3 类的组织
- **访问控制**：按 public、protected、private 顺序组织
- **成员变量**：放在类的最后，使用 private 访问控制
- **成员函数**：按功能分组，逻辑相关的函数放在一起

## 5. 最佳实践

### 5.1 异常处理
- **使用场景**：处理可能出现的错误情况
- **风格**：使用 try-catch 块，捕获具体的异常类型
- **示例**：
  ```cpp
  try
  {
      // 可能抛出异常的代码
  }
  catch (const std::exception& e)
  {
      LOG_ERROR("Error: " + std::string(e.what()));
  }
  ```

### 5.2 内存管理
- **智能指针**：优先使用 `std::unique_ptr`、`std::shared_ptr` 等智能指针
- **避免内存泄漏**：确保所有动态分配的内存都能被正确释放

### 5.3 代码可读性
- **函数长度**：单个函数不宜过长，建议不超过 50 行
- **代码复杂度**：避免过于复杂的逻辑，适当拆分函数
- **命名清晰**：变量和函数名应清晰表达其用途

### 5.4 性能优化
- **避免不必要的拷贝**：使用引用传递大对象
- **合理使用 const**：对不需要修改的变量和函数使用 const 修饰
- **避免频繁的内存分配**：使用对象池或预分配内存

## 6. 工具使用

### 6.1 代码格式化
- **工具**：clang-format（已验证版本 18）
- **配置文件**：`.clang-format`，基线 `BasedOnStyle: Google` + 项目定制（4 空格缩进、Allman 大括号、120 列、`Standard: c++17`）
- **使用方法**：
  ```bash
  clang-format -i file.cpp
  ```
- **全量格式化**：
  ```bash
  clang-format -i src/**/*.cpp src/**/*.h
  ```

### 6.2 静态分析
- **工具**：clang-tidy（已验证版本 18）
- **配置文件**：`.clang-tidy`，当前启用 `readability-identifier-naming` 落地第 1 章的命名规则
- **使用方法**：
  ```bash
  clang-tidy -p build src/xxx.cpp
  ```
- **存量代码迁移**：命名规则变更（`MAX_BUFFER_SIZE` → `kMaxBufferSize`、`SUCCESS` → `kSuccess`、`Idle` → `kIdle`）会命中大量历史代码，建议按模块用 `--fix` 批量重命名：
  ```bash
  clang-tidy -p build --fix src/base/xxx.cpp
  ```
  无法自动修复的位置用 `// NOLINT(readability-identifier-naming)` 标记，新代码一律直接按新规则编写

### 6.3 代码审查
- **流程**：代码提交前进行代码审查
- **重点**：命名规范、代码风格、逻辑正确性、性能优化

## 7. 版本控制

### 7.1 提交信息
- **格式**：[模块] 简短描述
- **示例**：
  ```
  [config] 添加视频日志配置项
  [network] 修复连接问题
  ```

### 7.2 分支管理
- **主分支**：main，稳定版本
- **开发分支**：dev，开发中的版本
- **功能分支**：feature/xxx，新功能开发
- **修复分支**：fix/xxx，bug 修复

## 8. 总结

本编码规范旨在确保代码的一致性、可读性和可维护性。所有团队成员应严格遵守本规范，以提高代码质量和开发效率。

如有任何疑问或建议，请及时提出，以便不断完善本规范。