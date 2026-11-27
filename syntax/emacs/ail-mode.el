;;; ail-mode.el --- Major mode for AIL (AI Language) -*- lexical-binding: t; -*-

;;; Commentary:
;; Syntax highlighting and basic indentation for AIL (.ail) source files.
;;
;; Installation:
;;   (add-to-list 'load-path "/path/to/ail/syntax/emacs")
;;   (require 'ail-mode)

;;; Code:

(defvar ail-mode-syntax-table
  (let ((st (make-syntax-table)))
    (modify-syntax-entry ?# "<" st)
    (modify-syntax-entry ?\n ">" st)
    (modify-syntax-entry ?\" "\"" st)
    (modify-syntax-entry ?' "\"" st)
    (modify-syntax-entry ?_ "w" st)
    (modify-syntax-entry ?? "w" st)
    (modify-syntax-entry ?! "w" st)
    st)
  "Syntax table for `ail-mode'.")

(defconst ail-font-lock-keywords
  (list
   ;; Core & AI declaration keywords
   (cons (regexp-opt
          '("def" "end" "class" "module" "include" "expect" "require" "require_relative"
            "if" "elsif" "else" "unless" "case" "when" "while" "until" "for" "in" "do"
            "begin" "rescue" "ensure" "raise" "return" "break" "next" "yield"
            "and" "or" "not"
            "schema" "tool" "guard" "skill" "decide" "agent" "flow"
            "within" "retry_up_to" "mcp_server" "a2a_agent")
          'words)
         'font-lock-keyword-face)
   ;; AI DSL directives
   (cons (regexp-opt
          '("config" "errors" "field" "param" "returns" "capabilities" "limits"
            "model" "system" "tools" "mcp" "skills" "guards" "context_view"
            "check_input" "check_output" "check_tool"
            "instructions" "choice" "score" "prob"
            "start" "node" "edge" "expose" "resource" "prompt" "handle")
          'words)
         'font-lock-preprocessor-face)
   ;; Builtins
   (cons (regexp-opt
          '("puts" "print" "eputs" "gets" "sleep_ms"
            "budget" "read_only" "read_write"
            "allow" "reject" "redact" "escalate")
          'words)
         'font-lock-builtin-face)
   ;; Constants
   (cons (regexp-opt '("true" "false" "nil" "self") 'words)
         'font-lock-constant-face)
   ;; Built-in types & modules
   (cons (regexp-opt
          '("Int" "Float" "Decimal" "Bool" "String" "Array" "Hash" "Range" "Regex"
            "Symbol" "Duration" "Any" "Enum"
            "Context" "LogView" "Knowledge" "MCP" "Config"
            "File" "Files" "Path" "OS" "JSON" "Env" "Time" "Math" "Random"
            "RuntimeError" "TypeError" "NoMethodError" "KeyError" "SchemaError"
            "PermissionError" "BudgetExceeded" "GuardViolation" "GuardEscalation"
            "ConfigError" "LLMError" "System1Error" "MCPError" "ToolError"
            "EmbedderError" "VectorError" "FlowError")
          'words)
         'font-lock-type-face)
   ;; Function & class/AI construct names
   '("\\<def\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*[?!]?\\)" 1 font-lock-function-name-face)
   '("\\<\\(?:class\\|module\\|schema\\|guard\\|agent\\|flow\\)\\s-+\\([A-Z][a-zA-Z0-9_]*\\)"
     1 font-lock-type-face)
   ;; Symbols, keyword labels, ivars, typed resource registries, flow arrow
   '("@[a-zA-Z_][a-zA-Z0-9_]*" . font-lock-variable-name-face)
   '("\\$\\(?:llm\\|s1\\|embedder\\|db\\|vector_db\\|mcp\\|skills\\)\\>" . font-lock-builtin-face)
   '("\\(?:^\\|[^:a-zA-Z0-9_]\\)\\(:[a-zA-Z_][a-zA-Z0-9_]*[?!]?\\)" 1 font-lock-constant-face)
   '("\\<[a-zA-Z_][a-zA-Z0-9_]*[?!]?:" . font-lock-constant-face)
   '("-->" . font-lock-keyword-face))
  "Font-lock highlighting rules for `ail-mode'.")

;;;###autoload
(define-derived-mode ail-mode prog-mode "AIL"
  "Major mode for editing AIL (AI Language) source files."
  :syntax-table ail-mode-syntax-table
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local tab-width 2)
  (setq-local indent-tabs-mode nil)
  (setq-local font-lock-defaults '(ail-font-lock-keywords)))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.ail\\'" . ail-mode))

(provide 'ail-mode)
;;; ail-mode.el ends here
