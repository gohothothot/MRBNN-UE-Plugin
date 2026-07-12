import pathlib
import runpy


project_script = pathlib.Path(__file__).resolve().parents[5] / "Projects" / "MRBNNExample" / "Scripts" / "SetupMRBNNExample.py"
runpy.run_path(str(project_script), run_name="__main__")
