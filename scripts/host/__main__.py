import sys
from pathlib import Path

# When launched as `python -m host` from outside scripts/, put scripts/ on path.
_SCRIPTS_ROOT = Path(__file__).resolve().parent.parent
if str(_SCRIPTS_ROOT) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS_ROOT))

from host.main import main

raise SystemExit(main())
