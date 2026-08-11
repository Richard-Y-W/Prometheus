# Threat model

Untrusted inputs include URLs, redirects, webpages, PDFs, STEP files, archives, filenames, document instructions, LLM output, and solver output. Controls include allowlisted schemes/hosts, redirect and size budgets, content/signature checks, sanitized generated artifact names, archive expansion limits, parser isolation, process CPU/memory/time limits, cancellation, and project-scoped authorization.

Source documents are data, never instructions. Downloaded binaries, macros, CAD scripts, and embedded code are never executed. Secrets remain server-side and are excluded from logs, manifests, prompts returned to clients, and diagnostic exports. External solver/cloud uploads require explicit disclosure and authorization.
