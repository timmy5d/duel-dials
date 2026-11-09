# PlatformIO pre-build hook: keep src/webapp.h in step with web/app.html.
Import("env")  # noqa: F821
import subprocess, sys, os
subprocess.check_call([sys.executable, os.path.join(env["PROJECT_DIR"], "tools", "make_web.py")])  # noqa: F821
