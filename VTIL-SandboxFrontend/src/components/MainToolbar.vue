<template>
    <div class="navbar">
        <label class="upload-btn">
            Upload VTIL
            <input type="file" accept=".vtil" @change="onFileChanged" />
        </label>
        <button class="ghost-btn" @click="$emit('refresh')">Refresh</button>
        <button class="ghost-btn" @click="$emit('toggle-theme')">{{ darkMode ? 'Light' : 'Dark' }}</button>
        <span class="status" :class="{ok: backendReady, err: !backendReady}">
            {{ backendReady ? 'API online' : 'API offline' }}
        </span>
        <span class="meta">File: <code>{{ routine.file_name || 'n/a' }}</code></span>
        <span class="meta">Entry: <code>{{ routine.entry_point || 'n/a' }}</code></span>
        <span class="meta">Blocks: <b>{{ blockCount }}</b></span>
        <span v-if="routine.last_error" class="error">{{ routine.last_error }}</span>
    </div>
</template>

<script>
export default {
    props: {
        routine: {
            type: Object,
            required: true
        },
        backendReady: {
            type: Boolean,
            default: false
        },
        darkMode: {
            type: Boolean,
            default: true
        }
    },
    computed: {
        blockCount() {
            return (this.routine.blocks || []).length
        }
    },
    methods: {
        onFileChanged(evt) {
            const file = evt.target.files && evt.target.files[0]
            if (file) this.$emit('upload-file', file)
            evt.target.value = ''
        }
    }
}
</script>

<style scoped>
.navbar {
    position: relative;
    padding: 0.55em 0.7em;
    box-sizing: border-box;
    width: 100%;
    margin: 0;
    border-bottom: 1px solid var(--panel-border);
    z-index: 100;
    background: var(--panel);
    display: flex;
    gap: 0.6em;
    align-items: center;
    flex-wrap: wrap;
    font-size: 13px;
    color: var(--text);
}
.upload-btn {
    border: 1px solid var(--panel-border);
    padding: 0.24em 0.62em;
    border-radius: 4px;
    cursor: pointer;
    background: var(--panel-2);
}
.upload-btn input {
    display: none;
}
.ghost-btn {
    border: 1px solid var(--panel-border);
    background: var(--panel-2);
    color: var(--text);
    border-radius: 4px;
    padding: 0.24em 0.62em;
    cursor: pointer;
}
.meta {
    color: var(--muted);
    white-space: nowrap;
}
.status.ok {
    color: #34d399;
}
.status.err,
.error {
    color: #f87171;
}

@media (max-width: 920px) {
    .navbar {
        gap: 0.4em;
        font-size: 12px;
    }
    .error {
        width: 100%;
    }
}
</style>