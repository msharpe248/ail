" ftplugin/ail.vim — Filetype plugin for AIL

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal shiftwidth=2
setlocal softtabstop=2
setlocal tabstop=2
setlocal expandtab
setlocal commentstring=#\ %s
setlocal comments=:#
setlocal iskeyword+=?,!

" Support % jumping with matchit
if exists("loaded_matchit") && !exists("b:match_words")
  let b:match_ignorecase = 0
  let b:match_words =
        \ '\<\(def\|class\|module\|expect\|schema\|tool\|guard\|skill\|decide\|agent\|flow\|within\|retry_up_to\|mcp_server\|a2a_agent\|if\|unless\|case\|while\|until\|begin\|do\)\>:' .
        \ '\<\(elsif\|else\|when\|rescue\|ensure\|errors\)\>:' .
        \ '\<end\>'
endif

let b:undo_ftplugin = "setlocal sw< sts< ts< et< cms< com< isk< | unlet! b:match_words b:match_ignorecase"
