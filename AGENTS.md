# Regras de trabalho

- Antes de cada commit neste repositório, incremente o número do comentário `# Versão de deploy: N` no topo de `backend/api_python/main.py` e inclua esse arquivo no commit. Esta regra foi solicitada pelo usuário porque seu Render monitora alterações nesse arquivo para disparar deploys.
- Não faça commit nem push sem autorização explícita do usuário. O marcador não autoriza publicação nem mudanças de branch.
- Use `b.lucasdxl` para alterações e commits, salvo pedido explícito para outra branch. O Render só recebe atualizações da branch configurada nele; não incorpore alterações à `main` automaticamente.
