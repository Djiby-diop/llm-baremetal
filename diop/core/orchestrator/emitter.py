from __future__ import annotations

import json
from pathlib import Path

from ..contracts.types import ExecutionReport


class WorkspaceEmitter:
    """Takes validated artifacts from an ExecutionReport and writes them to the physical file system."""
    
    def __init__(self, root_dir: str = "diop_workspace"):
        self.root = Path(root_dir)
        
    def emit(self, report: ExecutionReport) -> list[str]:
        # Only write physical files if human/system approved
        if report.validation.status not in ("approved", "approved_with_changes"):
            return []
            
        run_dir = self.root / report.run_id
        run_dir.mkdir(parents=True, exist_ok=True)
        
        written_paths = []
        for result in report.results:
            for artifact in result.artifacts:
                filename = artifact.get("name", f"artifact_{result.worker}")
                content = artifact.get("content", "")
                
                # Intelligent extension detection if not provided
                if "." not in filename:
                    if isinstance(content, (dict, list)):
                        filename += ".json"
                    elif result.worker == "code":
                        # Simplistic heuristic for code
                        if isinstance(content, str):
                            if "def " in content or "import " in content:
                                filename += ".py"
                            elif "#include" in content:
                                filename += ".c"
                            else:
                                filename += ".txt"
                    else:
                        filename += ".md"
                    
                file_path = run_dir / filename
                
                with file_path.open("w", encoding="utf-8") as f:
                    if isinstance(content, (dict, list)):
                        json.dump(content, f, indent=2, ensure_ascii=False)
                    else:
                        f.write(str(content))
                        
                written_paths.append(str(file_path))
                
        return written_paths
