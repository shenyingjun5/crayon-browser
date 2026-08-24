# Vendored: md4c

- 上游：`https://github.com/mity/md4c`
- 锁定版本：`release-0.5.3`（tag 对象 `093c3f45ce44bd6661849982b1dd7f0e7f385621`，指向提交 `472c417005c2c71b8617de4f7b8d6b30411d78f4`）
- 许可证：MIT（见 `LICENSE.md`，原样保留）
- vendor 日期：2026-08-24（`MDV-02`）
- 契约依据：`docs/current/markdown-viewer.md` §12 选型评审结论
- 文件清单与 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `src/md4c.c` | `f12907817a17ae7d0f6c8d18770df839f187cad5649dd36a475dba0675c5c1f8` |
| `src/md4c.h` | `4efd19bf7ec270691d5b4189f496886e421768a814b5e817eb945aa85e859f18` |
| `src/entity.c` | `002005fea65257438d04d6f16c060bd73e0e0355dbb31c3f40814ab5aa75a935` |
| `src/entity.h` | `7d021cb683c0e7643df6ce0db51d75f8cbc313be9ec754cb4dc18c40ebaa400b` |
| `LICENSE.md` | `d30937367d5413e7eaa218b1640b8946ff76fd34d97152f6979fd96169d5d0fc` |

## 修改纪律

本目录文件**不得就地修改**。升级/换版必须：更新 `VENDORED.md`（新 tag、新哈希）→ 重跑 `MDV-002` golden 与注入矩阵 → 在 MDV Roadmap 记录协议化变更评审。

## 构建接入

`browser/shared-ui/markdown/CMakeLists.txt` 将 `src/*.c` 编译为独立静态库 `crayon-vendored-md4c`（不套用产品 `-Werror` 口径，第三方代码警告隔离）；产品包装层 `crayon-browser-markdown` 以 `-Werror` 编译并只依赖其公开头。
