# mLRS Codex Notes

## Version control

- Репозиторий colocated: `.git` + `.jj`. Все изменения истории выполнять через
  `jj` (`jj status`, `jj diff`, `jj commit`); не использовать `git add` и
  `git commit`.
- `git` допустим для read-only inspection и совместимости с внешними tools.
- После `jj commit` отдельно проверять положение bookmark и не выполнять push
  без прямого запроса пользователя.
