from __future__ import annotations

import argparse
from pathlib import Path

from .core.orchestrator.service import DIOPOrchestrator
from .paths import default_memory_root


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the DIOP pipeline.")
    
    subparsers = parser.add_subparsers(dest="command", help="DIOP commands")

    # Models command
    models_parser = subparsers.add_parser("models", help="Manage the local model registry.")
    models_sub = models_parser.add_subparsers(dest="models_cmd", help="models subcommands")
    models_sub.add_parser("list", help="List registered models.")
    add_parser = models_sub.add_parser("add", help="Register a local model path under a short name.")
    add_parser.add_argument("name", help="Model name (e.g. tinyllama:q4km, llama2c:dev)")
    add_parser.add_argument("path", help="Path to model file (gguf/bin).")
    add_parser.add_argument("--format", default="gguf", help="Model format hint (default: gguf).")
    rm_parser = models_sub.add_parser("remove", help="Remove a registered model by name.")
    rm_parser.add_argument("name", help="Model name to remove.")
    show_parser = models_sub.add_parser("show", help="Show a registered model or blueprint.")
    show_parser.add_argument("name", help="Model or blueprint name.")
    create_parser = models_sub.add_parser("create", help="Create or update a model blueprint.")
    create_parser.add_argument("name", help="Blueprint name.")
    create_parser.add_argument("--from-model", required=True, dest="from_model", help="Base model name.")
    create_parser.add_argument("--system", default="", help="System prompt.")
    create_parser.add_argument("--template", default="", help="Prompt template.")
    models_sub.add_parser("blueprints", help="List saved blueprints.")

    # Gateway command (runtime HTTP surface)
    gateway_parser = subparsers.add_parser("gateway", help="Start the DIOP runtime HTTP gateway.")
    gateway_sub = gateway_parser.add_subparsers(dest="gateway_cmd", help="gateway subcommands")
    serve_parser = gateway_sub.add_parser("serve", help="Start the DIOP runtime HTTP gateway.")
    serve_parser.add_argument("--host", default="127.0.0.1")
    serve_parser.add_argument("--port", default=11434, type=int)
    serve_parser.add_argument("--adapter", choices=("mock", "native"), default="mock")
    gateway_sub.add_parser("doctor", help="Print a local runtime diagnostic snapshot.")

    # Profile command
    profile_parser = subparsers.add_parser("profile", help="Manage the active user profile and personal twin.")
    profile_sub = profile_parser.add_subparsers(dest="profile_cmd", help="profile subcommands")
    profile_sub.add_parser("show", help="Show the active user profile.")
    set_parser = profile_sub.add_parser("set", help="Set the active user profile.")
    set_parser.add_argument("--role", required=True, help="Primary user role.")
    set_parser.add_argument("--workspace-style", default="", dest="workspace_style", help="Workspace look and feel.")
    set_parser.add_argument("--focus", action="append", default=[], help="Focus area. Can be repeated.")
    profile_sub.add_parser("clear", help="Clear the active user profile.")
    profile_sub.add_parser("twin", help="Show the current personal twin snapshot.")

    # First boot command
    boot_parser = subparsers.add_parser("boot", help="Prepare the first personalized DIOP setup.")
    boot_sub = boot_parser.add_subparsers(dest="boot_cmd", help="boot subcommands")
    boot_setup = boot_sub.add_parser("setup", help="Seed profile, twin, and a guarded workspace proposal.")
    boot_setup.add_argument("--role", required=True, help="Primary user role.")
    boot_setup.add_argument("--workspace-style", default="", dest="workspace_style", help="Workspace look and feel.")
    boot_setup.add_argument("--focus", action="append", default=[], help="Focus area. Can be repeated.")
    boot_setup.add_argument("--preference", action="append", default=[], help="Preference as key=value. Can be repeated.")

    # System writer command
    system_parser = subparsers.add_parser("system", help="Manage human-approved DIOP system evolution proposals.")
    system_sub = system_parser.add_subparsers(dest="system_cmd", help="system subcommands")
    propose_parser = system_sub.add_parser("propose", help="Create a pending system change proposal.")
    propose_parser.add_argument("--title", required=True, help="Short proposal title.")
    propose_parser.add_argument("--goal", required=True, help="What the change should accomplish.")
    propose_parser.add_argument("--summary", default="", help="Implementation summary or reasoning.")
    propose_parser.add_argument("--file", action="append", default=[], dest="files", help="Impacted file. Can be repeated.")
    propose_parser.add_argument("--risk", default="medium", dest="risk_level", help="Risk level hint.")
    list_parser = system_sub.add_parser("list", help="List system change proposals.")
    list_parser.add_argument("--status", default="", help="Filter by pending, approved, or rejected.")
    show_proposal_parser = system_sub.add_parser("show", help="Show a system change proposal.")
    show_proposal_parser.add_argument("id", help="Proposal id.")
    patch_parser = system_sub.add_parser("patch", help="Attach a unified diff patch to a proposal.")
    patch_parser.add_argument("id", help="Proposal id.")
    patch_source = patch_parser.add_mutually_exclusive_group(required=True)
    patch_source.add_argument("--patch-file", help="Path to a unified diff file.")
    patch_source.add_argument("--patch-text", help="Unified diff text.")
    approve_parser = system_sub.add_parser("approve", help="Approve a pending system change proposal.")
    approve_parser.add_argument("id", help="Proposal id.")
    reject_parser = system_sub.add_parser("reject", help="Reject a system change proposal.")
    reject_parser.add_argument("id", help="Proposal id.")
    apply_parser = system_sub.add_parser("apply", help="Apply the approved patch attached to a proposal.")
    apply_parser.add_argument("id", help="Proposal id.")
    apply_parser.add_argument("--repo-root", default=str(Path.cwd()), help="Repository root where the patch should apply.")
    
    # Run command
    run_parser = subparsers.add_parser("run", help="Run a normal DIOP orchestration task.")
    run_parser.add_argument("goal", help="User goal to orchestrate.")
    run_parser.add_argument("--mode", choices=("auto", "solar", "lunar"), default="auto")
    run_parser.add_argument("--adapter", default="mock")
    run_parser.add_argument("--memory-root", default=str(default_memory_root()))
    run_parser.add_argument("--auto-approve", action="store_true")
    
    # Sleep command
    sleep_parser = subparsers.add_parser("sleep", help="Run the DIOP Sleep Engine to consolidate memory.")
    sleep_parser.add_argument("--adapter", default="local")
    sleep_parser.add_argument("--memory-root", default=str(default_memory_root()))
    
    # Daemon command
    daemon_parser = subparsers.add_parser("daemon", help="Run DIOP in fully autonomous background mode.")
    daemon_parser.add_argument("--adapter", default="local")
    daemon_parser.add_argument("--memory-root", default=str(default_memory_root()))

    # Train command
    train_parser = subparsers.add_parser("train", help="Train the DIOP native model on memory data.")
    train_parser.add_argument("--profile", choices=("micro", "mini", "base"), default="micro", help="Model size profile.")
    train_parser.add_argument("--memory-root", default=str(default_memory_root()))
    train_parser.add_argument("--output-dir", default=str(Path(__file__).resolve().parent / "engine" / "model"))
    train_parser.add_argument("--tokenizer", default=None, help="Path to tokenizer.model or tokenizer.bin")
    train_parser.add_argument("--dataset", default=None, help="Specific JSONL file in memory root to train on.")
    train_parser.add_argument("--output-name", default="diop_model", help="Base name for the output files (.pt, .bin).")
    
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    # For backward compatibility if run without a subcommand
    if not args.command:
        # We manually shift arguments to simulate 'run' if no subcommand was provided
        if argv and len(argv) > 0 and argv[0] not in ("run", "sleep", "-h", "--help"):
            args = parser.parse_args(["run"] + (argv if argv else []))
        else:
            parser.print_help()
            return 1

    if args.command == "models":
        from .model_blueprints import get_blueprint, list_blueprints, remove_blueprint, upsert_blueprint
        from .model_store import add_model, get_model, list_models, remove_model

        if args.models_cmd == "add":
            entry = add_model(args.name, Path(args.path), model_format=args.format)
            print(f"Registered model '{entry.name}' -> {entry.path}")
            return 0

        if args.models_cmd == "remove":
            removed = remove_model(args.name)
            if removed or remove_blueprint(args.name):
                print(f"Removed model '{args.name}'.")
                return 0
            print(f"Model '{args.name}' not found.")
            return 1

        if args.models_cmd == "create":
            entry = upsert_blueprint(
                name=args.name,
                base_model=args.from_model,
                system=args.system,
                template=args.template,
            )
            print(f"Blueprint '{entry.name}' now targets '{entry.base_model}'.")
            return 0

        if args.models_cmd == "show":
            model = get_model(args.name)
            if model is not None:
                print(f"name\t{model.name}")
                print(f"kind\tregistered-model")
                print(f"format\t{model.format}")
                print(f"path\t{model.path}")
                return 0

            blueprint = get_blueprint(args.name)
            if blueprint is not None:
                print(f"name\t{blueprint.name}")
                print(f"kind\tblueprint")
                print(f"from\t{blueprint.base_model}")
                print(f"system\t{blueprint.system}")
                print(f"template\t{blueprint.template}")
                return 0

            print(f"Model or blueprint '{args.name}' not found.")
            return 1

        if args.models_cmd == "blueprints":
            items = list_blueprints()
            if not items:
                print("No blueprints saved. Use: python -m diop models create <name> --from-model <model>")
                return 0
            for item in items:
                print(f"{item.name}\t{item.base_model}\t{item.created_at_unix}")
            return 0

        # Default: list
        items = list_models()
        if not items:
            print("No models registered. Use: python -m diop models add <name> <path>")
            return 0
        for m in items:
            print(f"{m.name}\t{m.format}\t{m.path}")
        return 0

    if args.command == "gateway":
        if args.gateway_cmd == "doctor":
            from .gateway.engine_pool import RuntimeEnginePool

            snapshot = RuntimeEnginePool().runtime_snapshot(adapter="mock")
            print(f"adapter\t{snapshot.get('adapter')}")
            pool = snapshot.get("pool", {})
            if isinstance(pool, dict):
                print(f"resident_slots\t{pool.get('resident_slots', 0)}")
                print(f"loads\t{pool.get('loads', 0)}")
                print(f"load_failures\t{pool.get('load_failures', 0)}")
                print(f"generations\t{pool.get('generations', 0)}")
            native = snapshot.get("native_runtime", {})
            if isinstance(native, dict):
                print(f"native_status\t{native.get('status', '')}")
                print(f"native_library\t{native.get('library_path', '')}")
                print(f"native_error\t{native.get('error', '')}")
            return 0

        if args.gateway_cmd != "serve":
            print("Usage: python -m diop gateway serve [--host 127.0.0.1] [--port 11434] [--adapter mock|native]")
            print("   or: python -m diop gateway doctor")
            return 1
        from .gateway.server import serve

        serve(host=args.host, port=args.port, adapter=args.adapter)
        return 0

    if args.command == "profile":
        from .profile_store import clear_active_profile, load_active_profile, save_active_profile
        from .twin_store import load_personal_twin, sync_twin_from_profile

        if args.profile_cmd == "set":
            profile = save_active_profile(
                role=args.role,
                workspace_style=args.workspace_style,
                focus=args.focus,
            )
            twin = sync_twin_from_profile()
            print(f"role\t{profile.get('role', '')}")
            print(f"workspace_style\t{profile.get('workspace_style', '')}")
            print(f"focus\t{','.join(profile.get('focus', []))}")
            print(f"twin_status\t{twin.get('status', '')}")
            return 0

        if args.profile_cmd == "clear":
            cleared = clear_active_profile()
            print("cleared\ttrue" if cleared else "cleared\tfalse")
            return 0

        if args.profile_cmd == "twin":
            twin = load_personal_twin()
            print(f"id\t{twin.get('id', '')}")
            print(f"profile_role\t{twin.get('profile_role', '')}")
            print(f"status\t{twin.get('status', '')}")
            print(f"behavior_markers\t{','.join(twin.get('behavior_markers', []))}")
            return 0

        profile = load_active_profile()
        print(f"id\t{profile.get('id', '')}")
        print(f"role\t{profile.get('role', '')}")
        print(f"workspace_style\t{profile.get('workspace_style', '')}")
        print(f"focus\t{','.join(profile.get('focus', []))}")
        return 0

    if args.command == "boot":
        from .first_boot import prepare_first_boot

        if args.boot_cmd != "setup":
            print("Usage: python -m diop boot setup --role <role> [--workspace-style ...] [--focus ...]")
            return 1
        preferences: dict[str, object] = {}
        for item in args.preference:
            key, sep, value = str(item).partition("=")
            if sep and key.strip():
                preferences[key.strip()] = value.strip()
        try:
            plan = prepare_first_boot(
                role=args.role,
                workspace_style=args.workspace_style,
                focus=args.focus,
                preferences=preferences,
            )
        except ValueError as e:
            print(f"Boot setup failed: {e}")
            return 1
        print(f"role\t{plan.profile.get('role', '')}")
        print(f"workspace_style\t{plan.profile.get('workspace_style', '')}")
        print(f"twin_status\t{plan.twin.get('status', '')}")
        print(f"proposal\t{plan.proposal.id}")
        print(f"modules\t{','.join(plan.recommended_modules)}")
        return 0

    if args.command == "system":
        from .system_writer import apply_proposal, attach_patch, create_proposal, get_proposal, list_proposals, update_proposal_status

        if args.system_cmd == "propose":
            proposal = create_proposal(
                title=args.title,
                goal=args.goal,
                summary=args.summary,
                files=args.files,
                risk_level=args.risk_level,
            )
            print(f"id\t{proposal.id}")
            print(f"status\t{proposal.status}")
            print(f"title\t{proposal.title}")
            print(f"risk\t{proposal.risk_level}")
            return 0

        if args.system_cmd == "show":
            proposal = get_proposal(args.id)
            if proposal is None:
                print(f"Proposal '{args.id}' not found.")
                return 1
            print(f"id\t{proposal.id}")
            print(f"status\t{proposal.status}")
            print(f"title\t{proposal.title}")
            print(f"goal\t{proposal.goal}")
            print(f"summary\t{proposal.summary}")
            print(f"files\t{','.join(proposal.files)}")
            print(f"risk\t{proposal.risk_level}")
            print(f"patch_status\t{proposal.patch_status}")
            if proposal.patch_text:
                print("patch")
                print(proposal.patch_text.rstrip())
            return 0

        if args.system_cmd == "patch":
            if args.patch_file:
                patch_text = Path(args.patch_file).read_text(encoding="utf-8")
            else:
                patch_text = args.patch_text
            proposal = attach_patch(args.id, patch_text)
            if proposal is None:
                print(f"Proposal '{args.id}' not found.")
                return 1
            print(f"id\t{proposal.id}")
            print(f"patch_status\t{proposal.patch_status}")
            print(f"patch_bytes\t{len(proposal.patch_text.encode('utf-8'))}")
            return 0

        if args.system_cmd in ("approve", "reject"):
            status = "approved" if args.system_cmd == "approve" else "rejected"
            proposal = update_proposal_status(args.id, status)
            if proposal is None:
                print(f"Proposal '{args.id}' not found.")
                return 1
            print(f"id\t{proposal.id}")
            print(f"status\t{proposal.status}")
            return 0

        if args.system_cmd == "apply":
            try:
                result = apply_proposal(args.id, Path(args.repo_root))
            except ValueError as e:
                print(f"Apply failed: {e}")
                return 1
            if result is None:
                print(f"Proposal '{args.id}' not found.")
                return 1
            proposal, changed = result
            print(f"id\t{proposal.id}")
            print(f"patch_status\t{proposal.patch_status}")
            for path in changed:
                print(f"changed\t{path}")
            return 0

        items = list_proposals(status=args.status or None)
        if not items:
            print("No system proposals saved.")
            return 0
        for proposal in items:
            print(f"{proposal.id}\t{proposal.status}\t{proposal.patch_status}\t{proposal.risk_level}\t{proposal.title}")
        return 0

    memory_root = Path(args.memory_root)

    if args.command == "sleep":
        from .evolution.sleep_learning import SleepLearningEngine
        engine = SleepLearningEngine(memory_root=memory_root, adapter_name=args.adapter)
        engine.run_sleep_cycle()
        return 0

    if args.command == "daemon":
        from .daemon import DIOPDaemon
        daemon = DIOPDaemon(memory_root=memory_root, adapter_name=args.adapter)
        daemon.start()
        return 0

    if args.command == "run":
        orchestrator = DIOPOrchestrator(memory_root=memory_root, adapter_name=args.adapter)
        report = orchestrator.run(goal=args.goal, mode=args.mode, auto_approve=args.auto_approve)
        return 0

    if args.command == "train":
        from .engine.model.trainer import DiopModelTrainer
        tokenizer_path = Path(args.tokenizer) if args.tokenizer else None
        trainer = DiopModelTrainer(
            memory_root=memory_root,
            output_dir=Path(args.output_dir),
            profile=args.profile,
            tokenizer_path=tokenizer_path,
            dataset_file=args.dataset,
            output_name=args.output_name,
        )
        trainer.run()
        return 0

    return 0
