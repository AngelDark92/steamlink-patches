# Project skill guidance

Repository-root and parent `AGENTS.md` apply to every skill here. When a skill
builds, compiles, audits, or delegates work, all generated output, scratch and
project caches must use `../builds/steamlink-patches/`, outside the repository.
Use the [build layout](../diagnostics/build-layout/README.md): Gradle output is
separate from retained fixture/tool/evidence inputs. Do not introduce local build
paths in examples, helper scripts, or subagent prompts. Communication-only skills
retain their existing behavior and inherit this requirement if they initiate work.
