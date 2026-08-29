# Documentation tools

Run the first-party Markdown and documentation-contract checker from the repository root:

```bash
python3 tools/docs/check_docs.py
```

On Windows:

```powershell
py tools\docs\check_docs.py
```

The checker validates:

- relative Markdown links;
- fenced code-block balance;
- duplicate headings within one file;
- required identity/result/scope language in validation records;
- the architectural rule that AI or host software must not enter the current processor bus cycle.

Historical policy exceptions are explicit in `docs/document_policy_exceptions.txt`. Structural failures such as broken links cannot be waived.

Run its unit tests with:

```bash
python3 -m unittest discover -s tests/docs -p "test_*.py"
```
