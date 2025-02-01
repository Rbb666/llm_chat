from building import *

cwd = GetCurrentDir()
path = [cwd]
src = Glob('*.c')

if GetDepend(['PKG_LLM_USING_QWEN_CLOUD']):
    path += [cwd + '/ports']
    src += Glob('ports/qwen_ai.c')

if GetDepend(['PKG_LLM_USING_DOUBAO_CLOUD']):
    path += [cwd + '/ports']
    src += Glob('ports/doubao_ai.c')

group = DefineGroup('llm', src, depend = ['PKG_USING_LLM'], CPPPATH = path)

Return('group')
