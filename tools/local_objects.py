"""Keep absolute-source object targets in this project's build directory."""

from pathlib import Path

import SCons.Defaults


def install(project_root, sdk_root):
    project = Path(project_root).resolve()
    sdk = Path(sdk_root).resolve()
    original = SCons.Defaults.StaticObjectEmitter

    def emit(target, source, env):
        build = Path(env["build_dir"]).resolve()
        if not build.is_relative_to(project):
            raise RuntimeError(f"Build directory outside project: {build}")
        relocated = []
        for node in target:
            path = Path(node.abspath).resolve()
            if not path.is_relative_to(build):
                if path.is_relative_to(project):
                    relative = Path("project_objects") / path.relative_to(project)
                elif path.is_relative_to(sdk):
                    relative = Path("sdk_objects") / path.relative_to(sdk)
                else:
                    raise RuntimeError(f"Object target outside project/SDK: {path}")
                node = env.File(str(build / relative))
            relocated.append(node)
        return original(relocated, source, env)

    SCons.Defaults.StaticObjectEmitter = emit
