use std::fs;

use osg_memory_warden::dplus::{
    compute_merit_profile, format_reasons_csv, parse, DPlusSection, SectionKind, SectionTag,
    for_each_op,
};

fn extract_ops(section_body: &str, out: &mut Vec<u32>) {
    for_each_op(section_body, |op| out.push(op));
}

fn sanitize_mermaid_id(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len());
    for ch in raw.chars() {
        if ch.is_ascii_alphanumeric() {
            out.push(ch);
        } else {
            out.push('_');
        }
    }
    if out.is_empty() {
        out.push('X');
    }
    out
}

fn tag_label(tag: SectionTag<'_>) -> String {
    match tag {
        SectionTag::Known(k) => format!("{:?}", k),
        SectionTag::Other(h) => h.to_string(),
    }
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: dplus_graph <file.dplus>");
        std::process::exit(2);
    });

    let src = fs::read_to_string(&path).unwrap_or_else(|e| {
        eprintln!("read failed: {e}");
        std::process::exit(2);
    });

    let mut scratch = [DPlusSection {
        tag: SectionTag::Known(SectionKind::Unknown),
        body: "",
    }; 256];

    let module = parse(&src, &mut scratch).unwrap_or_else(|e| {
        eprintln!("parse error: {e}");
        std::process::exit(1);
    });

    let merit = compute_merit_profile(&module);

    let mut intent_nodes: Vec<(String, String)> = Vec::new();
    let mut law_ops: Vec<u32> = Vec::new();
    let mut proof_ops: Vec<u32> = Vec::new();

    // Mermaid graph:
    // - Nodes: sections + per-op nodes
    // - Edges: INTENT -> MERIT -> LAW/PROOF ops, LAW op -> PROOF op for matching ids
    println!("graph TD");

    // Global merit node
    let mut merit_buf = [0u8; 64];
    let reasons = format_reasons_csv(merit.reasons, &mut merit_buf);
    println!(
        "  MERIT[\"MERIT\\nscore={}/100\\ndefault_sandbox={}\\nbytes_cap={:?}\\nttl_cap_ms={:?}\\nreasons={}\"]",
        merit.score_0_100,
        merit.default_sandbox,
        merit.bytes_cap,
        merit.ttl_cap_ms,
        reasons
    );

    for (idx, sec) in module.sections.iter().enumerate() {
        let label = tag_label(sec.tag);
        let id = format!("S{}__{}", idx, sanitize_mermaid_id(&label));
        let kind = sec
            .tag
            .kind()
            .map(|k| format!("{:?}", k))
            .unwrap_or_else(|| "Opaque".to_string());
        let bytes = sec.body.as_bytes().len();
        println!("  {}[\"{}\\n({})\\n{} bytes\"]", id, label, kind, bytes);

        if let SectionTag::Other(h) = sec.tag {
            if h.eq_ignore_ascii_case("INTENT") {
                let preview: String = sec.body.trim().chars().take(80).collect();
                intent_nodes.push((id.clone(), preview.replace('\n', " ")));
            }
        }

        match sec.tag.kind() {
            Some(SectionKind::Law) => extract_ops(sec.body, &mut law_ops),
            Some(SectionKind::Proof) => extract_ops(sec.body, &mut proof_ops),
            _ => {}
        }
    }

    law_ops.sort_unstable();
    law_ops.dedup();
    proof_ops.sort_unstable();
    proof_ops.dedup();

    // Add op nodes
    for id in &law_ops {
        println!("  OP_LAW_{}((\"op:{}\\nLAW\"))", id, id);
    }
    for id in &proof_ops {
        println!("  OP_PROOF_{}((\"op:{}\\nPROOF\"))", id, id);
    }

    // Intent edges (if any) -> MERIT -> LAW/PROOF op sets
    if !intent_nodes.is_empty() {
        for (intent_id, _preview) in intent_nodes {
            println!("  {} --> MERIT", intent_id);
        }
    }

    for id in &law_ops {
        println!("  MERIT --> OP_LAW_{}", id);
    }
    for id in &proof_ops {
        println!("  MERIT --> OP_PROOF_{}", id);
    }

    // Consensus edges: same op id appears in LAW and PROOF
    for id in &law_ops {
        if proof_ops.binary_search(id).is_ok() {
            println!("  OP_LAW_{} --> OP_PROOF_{}", id, id);
        }
    }
}
