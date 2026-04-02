<template>
<div id="main" :class="{ 'theme-dark': darkMode }">
    <main-toolbar
        :routine="routine"
        :backend-ready="backendReady"
        :dark-mode="darkMode"
        @upload-file="handleUpload"
        @download-file="downloadCurrentRoutine"
        @refresh="refreshRoutine"
        @toggle-theme="toggleTheme" />
    <splittable-pane
        id="main-pane"
        :routine="routine"
        :editor-schema="editorSchema"
        @edit-immediate="handleEditImmediate"
        @edit-instruction="handleEditInstruction" />
</div>
</template>
<script>
import MainToolbar from './components/MainToolbar'
import SplittablePane from './components/SplittablePane'

const API_BASE = 'http://127.0.0.1:8090'

export default {
    data() {
        return {
            backendReady: false,
            darkMode: true,
            routine: {
                ok: false,
                file_name: '',
                entry_point: null,
                blocks: [],
                cfg_edges: [],
                last_error: ''
            },
            editorSchema: {
                mnemonics: [],
                registers: [],
                instructions: []
            },
            pollTimer: null
        }
    },
    components: {
        'main-toolbar': MainToolbar,
        SplittablePane
    },
    methods: {
        buildApiUrl(path, query = {}) {
            const url = new URL(`${API_BASE}${path}`)
            Object.keys(query).forEach((key) => {
                if (query[key] !== undefined && query[key] !== null) {
                    url.searchParams.set(key, String(query[key]))
                }
            })
            return url.toString()
        },
        normalizeRoutine(payload) {
            const blocks = (payload.blocks || []).slice().sort((a, b) => {
                const aa = a.address || ''
                const bb = b.address || ''
                return aa.localeCompare(bb)
            })

            return {
                ok: !!payload.ok,
                file_name: payload.file_name || '',
                entry_point: payload.entry_point || null,
                blocks,
                cfg_edges: payload.cfg_edges || [],
                last_error: payload.last_error || ''
            }
        },
        async checkHealth() {
            try {
                const response = await fetch(`${API_BASE}/health`)
                this.backendReady = response.ok
            } catch {
                this.backendReady = false
            }
        },
        async refreshRoutine() {
            await this.checkHealth()
            if (!this.backendReady) return

            try {
                const response = await fetch(`${API_BASE}/api/state`)
                if (!response.ok) return
                const payload = await response.json()
                this.routine = this.normalizeRoutine(payload)
            } catch {
                this.backendReady = false
            }
        },
        async refreshEditorSchema() {
            await this.checkHealth()
            if (!this.backendReady) return

            try {
                const response = await fetch(this.buildApiUrl('/api/schema'))
                if (!response.ok) return
                const payload = await response.json()
                this.editorSchema = {
                    mnemonics: payload.mnemonics || [],
                    registers: payload.registers || [],
                    instructions: payload.instructions || []
                }
            } catch {
                this.backendReady = false
            }
        },
        async handleUpload(file) {
            if (!file) return

            await this.checkHealth()
            if (!this.backendReady) {
                window.alert('Backend is not reachable on http://127.0.0.1:8090')
                return
            }

            const buffer = await file.arrayBuffer()
            const response = await fetch(`${API_BASE}/api/upload?name=${encodeURIComponent(file.name)}`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/octet-stream'
                },
                body: buffer
            })

            if (!response.ok) {
                const text = await response.text()
                window.alert(text || 'Upload failed.')
            }
            await this.refreshRoutine()
            await this.refreshEditorSchema()
        },
        async handleEditImmediate(payload) {
            if (!payload) return

            await this.checkHealth()
            if (!this.backendReady) {
                window.alert('Backend is not reachable on http://127.0.0.1:8090')
                return
            }

            const endpoint = this.buildApiUrl('/api/edit/immediate', {
                block: payload.block,
                instruction: payload.instruction,
                operand: payload.operand,
                value: payload.value
            })

            try {
                const response = await fetch(endpoint, { method: 'POST' })
                if (!response.ok) {
                    const text = await response.text()
                    window.alert(text || 'Immediate edit failed.')
                }
            } catch {
                this.backendReady = false
                window.alert('Immediate edit failed.')
            }

            await this.refreshRoutine()
            await this.refreshEditorSchema()
        },
        async handleEditInstruction(payload) {
            if (!payload || !payload.text) return

            await this.checkHealth()
            if (!this.backendReady) {
                window.alert('Backend is not reachable on http://127.0.0.1:8090')
                return
            }

            const endpoint = this.buildApiUrl('/api/edit/instruction', {
                block: payload.block,
                instruction: payload.instruction
            })

            try {
                const response = await fetch(endpoint, {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'text/plain; charset=utf-8'
                    },
                    body: payload.text
                })

                if (!response.ok) {
                    const text = await response.text()
                    window.alert(text || 'Instruction edit failed.')
                }
            } catch {
                this.backendReady = false
                window.alert('Instruction edit failed.')
            }

            await this.refreshRoutine()
            await this.refreshEditorSchema()
        },
        async downloadCurrentRoutine() {
            await this.checkHealth()
            if (!this.backendReady) {
                window.alert('Backend is not reachable on http://127.0.0.1:8090')
                return
            }

            try {
                const response = await fetch(this.buildApiUrl('/api/download'))
                if (!response.ok) {
                    const text = await response.text()
                    window.alert(text || 'Download failed.')
                    return
                }

                const blob = await response.blob()
                const objectUrl = URL.createObjectURL(blob)
                const disposition = response.headers.get('Content-Disposition') || ''
                const match = disposition.match(/filename="?([^";]+)"?/i)
                const fallback = this.routine.file_name || 'edited.vtil'
                const filename = (match && match[1]) ? match[1] : fallback

                const anchor = document.createElement('a')
                anchor.href = objectUrl
                anchor.download = filename
                document.body.appendChild(anchor)
                anchor.click()
                anchor.remove()
                URL.revokeObjectURL(objectUrl)
            } catch {
                this.backendReady = false
                window.alert('Download failed.')
            }
        },
        toggleTheme() {
            this.darkMode = !this.darkMode
        }
    },
    mounted() {
        this.refreshRoutine()
        this.refreshEditorSchema()
        this.pollTimer = setInterval(() => {
            this.refreshRoutine()
        }, 1200)
    },
    beforeDestroy() {
        clearInterval(this.pollTimer)
    }
}

</script>
<style>
html,
body {
    height: 100%;
    margin: 0;
    font-family: -apple-system, 'Segoe UI', Ubuntu, Arial, sans-serif;
    background: #0b1220;
}
#app {
    height: 100%;
}
#main {
    --bg: #f4f6f8;
    --panel: #ffffff;
    --panel-2: #f8fafc;
    --panel-border: #d1d5db;
    --text: #0f172a;
    --muted: #64748b;
    --accent: #2563eb;
    --accent-soft: #dbeafe;
    --edge: #64748b;
    height: 100%;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    color: var(--text);
    background: var(--bg);
}
#main.theme-dark {
    --bg: #0b1220;
    --panel: #121a2b;
    --panel-2: #0f172a;
    --panel-border: #233148;
    --text: #dbe7ff;
    --muted: #9ab0cf;
    --accent: #60a5fa;
    --accent-soft: #1d355a;
    --edge: #7c8ba6;
}
#main-pane {
    position: relative;
    top: auto;
    flex: 1;
    min-height: 0;
}
</style>
