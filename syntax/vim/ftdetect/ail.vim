" ftdetect/ail.vim — Filetype detection for AIL (.ail) and AIL env (.ail.env)
au BufRead,BufNewFile *.ail     setfiletype ail
au BufRead,BufNewFile *.ail.env setlocal filetype=ailenv
