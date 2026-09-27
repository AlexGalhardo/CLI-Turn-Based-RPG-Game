# Origem das skills de terceiros

Skills copiadas (vendored) dos repositórios upstream. Para atualizar, clone o repo no commit desejado e substitua a pasta correspondente — não edite os arquivos à mão, para não divergir do upstream.

| Skill(s) | Repositório | Commit | Licença |
| --- | --- | --- | --- |
| `impeccable` (+ `.claude/agents/impeccable-*.md`) | [pbakaus/impeccable](https://github.com/pbakaus/impeccable) | `9d715cc4f5564a990ca8345abfdd5df6dc9b41c8` | Apache-2.0 |
| `ponytail`, `ponytail-audit`, `ponytail-debt`, `ponytail-gain`, `ponytail-help`, `ponytail-review` | [DietrichGebert/ponytail](https://github.com/DietrichGebert/ponytail) | `e3ba2aa6f1e6f0bc4d69eb09c9f0d0a93af56156` | MIT |
| `api-and-interface-design` … `using-agent-skills` (25 skills) + `.claude/references/*.md` + `.claude/agents/{code-reviewer,security-auditor,test-engineer,web-performance-auditor}.md` | [addyosmani/agent-skills](https://github.com/addyosmani/agent-skills) | `bcab6a1b8503100e8618c3b4e32cc78de43de769` | MIT |
| `graphify` (SKILL.md = `graphify/skill.md`, `references/` = `graphify/skills/claude/references`) | [Graphify-Labs/graphify](https://github.com/Graphify-Labs/graphify) | `4000de15466588ec3ee32f9e10a587ca97d3b8a5` | Apache-2.0 |
| `frontend-design` ([agenticskills.io](https://agenticskills.io/skills/frontend-design) aponta para o repo da Anthropic) | [anthropics/skills](https://github.com/anthropics/skills) | `33375500bcea98d610eb30ce10ac4e59b89c390d` | ver `frontend-design/LICENSE.txt` |

## Dependências externas

- **graphify** precisa do CLI Python `graphifyy` (versão pinada `0.9.68`): `pip install --user graphifyy==0.9.68` (ou `uv tool install graphifyy==0.9.68`). Saída em `graphify-out/` (ignorada no git).
- **impeccable** traz scripts próprios em `impeccable/scripts/`. Os hooks de `settings.json` do repositório upstream **não** foram instalados (rodariam em todo Edit/Write) — ative só se quiser o detector automático.

## Skills do próprio projeto

`respondeae-secure-endpoint` e `respondeae-local-verification` foram escritas neste repositório (Fase 13 do `PLAN.md`), a partir dos achados da auditoria (`docs/security.md`) e das armadilhas de validação encontradas.

