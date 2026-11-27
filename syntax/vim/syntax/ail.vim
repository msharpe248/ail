" Vim syntax file
" Language:     AIL (AI Language)
" Maintainer:   AIL Contributors
" Filenames:    *.ail

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

" Comments
syn keyword ailTodo contained TODO FIXME XXX NOTE HACK BUG
syn match   ailComment "#.*$" contains=ailTodo,@Spell

" Core language keywords
syn keyword ailControl      if elsif else unless case when while until for in do end
syn keyword ailControl      return break next yield
syn keyword ailException    begin rescue ensure raise
syn keyword ailDefine       def class module include
syn keyword ailImport       expect require require_relative
syn keyword ailOperatorWord and or not
syn keyword ailBoolean      true false
syn keyword ailNil          nil
syn keyword ailSelf         self

" First-class AI construct keywords
syn keyword ailAIKeyword    schema tool guard skill decide agent flow
syn keyword ailAIKeyword    within retry_up_to mcp_server a2a_agent

" AI DSL directives (contextual keywords inside blocks)
syn keyword ailAIDirective  config errors field param returns capabilities limits
syn keyword ailAIDirective  model system tools mcp skills guards context_view
syn keyword ailAIDirective  check_input check_output check_tool
syn keyword ailAIDirective  instructions choice score prob
syn keyword ailAIDirective  start node edge expose resource prompt handle

" Global builtins & guard/capability constructors
syn keyword ailBuiltin      puts print eputs gets sleep_ms
syn keyword ailBuiltin      budget read_only read_write
syn keyword ailBuiltin      allow reject redact escalate

" Built-in modules, AI runtime types, primitive types & exceptions
syn keyword ailType         Int Float Decimal Bool String Array Hash Range Regex Symbol Duration Any Enum
syn keyword ailType         Context LogView Knowledge MCP Config File Files Path OS JSON Env Time Math Random
syn keyword ailType         Agent Flow Schema Tool Guard GuardResult
syn keyword ailExceptionCls RuntimeError TypeError NoMethodError KeyError SchemaError
syn keyword ailExceptionCls PermissionError BudgetExceeded GuardViolation GuardEscalation
syn keyword ailExceptionCls ConfigError LLMError System1Error MCPError ToolError
syn keyword ailExceptionCls EmbedderError VectorError FlowError

" Function / method / class / AI construct names after declaration keyword
syn match   ailFunctionName "\<\(def\)\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*[?!]\?"
syn match   ailTypeName     "\<\(class\|module\|schema\|guard\|agent\|flow\)\s\+\zs[A-Z][a-zA-Z0-9_]*"

" Instance variables & typed resource registries ($llm, $s1, $db, $mcp, ...)
syn match   ailIvar         "@[a-zA-Z_][a-zA-Z0-9_]*"
syn match   ailResReg       "\$\(llm\|s1\|embedder\|db\|vector_db\|mcp\|skills\)\>"

" Keyword argument / hash labels (foo:) and symbols (:foo, :foo?)
syn match   ailLabel        "\<[a-zA-Z_][a-zA-Z0-9_]*[?!]\?:\ze[^:]"
syn match   ailSymbol       "\(^\|[^:a-zA-Z0-9_]\)\zs:[a-zA-Z_][a-zA-Z0-9_]*[?!]\?"

" Numbers: durations, exact decimals, floats, integers
syn match   ailDuration     "\<\d[0-9_]*\(\.\d[0-9_]*\)\?\.\(ms\|s\|m\|h\)\>"
syn match   ailDecimal      "\<\d[0-9_]*\(\.\d[0-9_]*\)\?d\>"
syn match   ailFloat        "\<\d[0-9_]*\.\d[0-9_]*\([eE][+-]\?\d\+\)\?\>"
syn match   ailInteger      "\<\d[0-9_]*\>"

" Operators (including flow arrow -->, lambda ->, hash rocket =>, regex =~ / !~, ranges)
syn match   ailFlowArrow    "-->"
syn match   ailOperator     "->\|=>\|=\~\|!\~\|&&\|||\|\.\.\.\|\.\.\|==\|!=\|<=\|>=\|+=\|-=\|\*=\|/=\|||="
syn match   ailOperator     "[+\-*/%<>=!&|?:]"

" Escapes & string interpolation
syn match   ailEscape       contained "\\\(x\x\x\|[nrt0e\\\"'#]\)"
syn region  ailInterpolation contained matchgroup=ailInterpDelim start="#{" end="}" contains=TOP

" Double-quoted and single-quoted strings
syn region  ailString       start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=ailEscape,ailInterpolation,@Spell
syn region  ailRawString    start=+'+ skip=+\\\\\|\\'+ end=+'+ contains=@Spell

" Heredocs (<<~PROMPT ... PROMPT, <<MD ... MD)
syn region  ailHeredoc      matchgroup=ailHeredocDelim start=+<<\~\?\z([A-Z_][A-Z0-9_]*\)+ end=+^\s*\z1$+ contains=ailInterpolation,@Spell

" Regex literals (/pattern/ims) — only after operators, parens, commas, or line start
syn region  ailRegex        start=+\(\(^\|\s\|[(\[,=~!]\)\s*\)\@<=//+ms=s+1 skip=+\\\\\|\\/+ end=+/+ oneline
syn region  ailRegex        start=+\(\(^\|\s\|[(\[,=~!]\)\s*\)\@<=/[^/* \t\n]+ skip=+\\\\\|\\/+ end=+/[ims]*+ oneline contains=ailEscape

" Highlight links
hi def link ailComment       Comment
hi def link ailTodo          Todo
hi def link ailControl       Conditional
hi def link ailException     Exception
hi def link ailDefine        Keyword
hi def link ailImport        Include
hi def link ailOperatorWord  Operator
hi def link ailBoolean       Boolean
hi def link ailNil           Constant
hi def link ailSelf          Identifier

hi def link ailAIKeyword     Statement
hi def link ailAIDirective   PreProc
hi def link ailBuiltin       Function
hi def link ailType          Type
hi def link ailExceptionCls  Type
hi def link ailFunctionName  Function
hi def link ailTypeName      Type

hi def link ailIvar          Identifier
hi def link ailResReg        Special
hi def link ailLabel         Constant
hi def link ailSymbol        Constant

hi def link ailDuration      Number
hi def link ailDecimal       Float
hi def link ailFloat         Float
hi def link ailInteger       Number

hi def link ailFlowArrow     Special
hi def link ailOperator      Operator

hi def link ailEscape        SpecialChar
hi def link ailInterpDelim   Delimiter
hi def link ailString        String
hi def link ailRawString     String
hi def link ailHeredoc       String
hi def link ailHeredocDelim  Special
hi def link ailRegex         Special

let b:current_syntax = "ail"

let &cpo = s:cpo_save
unlet s:cpo_save
