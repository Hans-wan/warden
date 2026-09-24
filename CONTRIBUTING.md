# Contributing to warden · 参与贡献

English · [简体中文](#简体中文)

## English

warden accepts contributions — every change lands through a reviewed pull request.

- **All changes go through pull requests.** Every PR is reviewed by the maintainer ([@Hans-wan](https://github.com/Hans-wan)) before merging; direct pushes to `master` are blocked by branch protection.
- **License.** By opening a pull request you agree that your contribution is licensed under the project's [MIT License](LICENSE).
- **Before opening a PR**, make sure the build and tests pass:

  ```bash
  cmake -B build -G Ninja && cmake --build build
  ctest --test-dir build --output-on-failure
  ```

- **Keep PRs focused.** One change per PR. Describe what it does and why.
- **Core semantics first.** If your change touches adjudication behavior, the capability taxonomy (`src/core/capability/tags.h`), or the journal format, open an issue describing the change before writing code.

## 简体中文

warden 接受外部贡献——所有改动一律通过 Pull Request 提交,经审核后合并。

- **所有改动走 PR。** 每个 PR 都会由维护者([@Hans-wan](https://github.com/Hans-wan))审核后才能合并;`master` 已开启分支保护,禁止直接推送。
- **授权。** 提交 PR 即表示同意你的贡献按本项目 [MIT License](LICENSE) 授权。
- **提 PR 之前**,先确保构建和测试通过:

  ```bash
  cmake -B build -G Ninja && cmake --build build
  ctest --test-dir build --output-on-failure
  ```

- **PR 保持聚焦。** 一个 PR 只做一件事,写清楚改了什么、为什么改。
- **核心语义改动先开 issue。** 涉及裁决逻辑、能力分类法(`src/core/capability/tags.h`)或 journal 格式的改动,请先开 issue 描述方案,再动手写代码。
