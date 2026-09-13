# Git 更新规范 (Git Workflow Rules)

## 核心规则 (Mandatory Rule)
**每次处理、修改代码或完成问题排查后，都必须更新 Git！**
Whenever any code is processed, bug fixed, or feature modified, the agent MUST update Git.

## 执行流程 (Execution Steps)
1. **核对改动**：运行 `git status` 与 `git diff` 审查修改内容，确保无多余临时文件或不相关改动。
2. **暂存文件**：执行 `git add <files>` 暂存修改及新增的相关文件。
3. **清晰提交**：执行 `git commit -m "<type>(<scope>): <message>"`，使用清晰的中文或英文语义化提交信息。
4. **工作区检查**：确认 `git status` 确保工作区状态符合预期。
