<template>
    <div :class="{container: true, horizontal: true, dragging: dragging}" ref="container">
        <div class="splittable" v-for="i in count" :key="i" :data-id="i" :style="{ width: sizes[i - 1] + '%' }">
            <div v-if="i === 1" class="pane-content">
                <h3>Blocks</h3>
                <div v-if="blockRows.length === 0" class="muted">Upload a .vtil file to view CFG blocks.</div>
                <div v-for="blk in blockRows" :key="blk.address" class="row" :class="{selected: selectedKey === blk.address}" @click="selectBlock(blk.address)">
                    <code>{{ blk.address }}</code>
                    <span>{{ blk.instruction_count || 0 }} insn</span>
                </div>
            </div>

            <div v-else class="pane-content">
                <h3>Details</h3>
                <div v-if="!selectedBlock" class="muted">Select a block from the left pane.</div>
                <div v-else>
                    <p><b>Address:</b> <code>{{ selectedBlock.address }}</code></p>
                    <p><b>Instructions:</b> {{ selectedBlock.instruction_count || 0 }}</p>
                    <p><b>Predecessors:</b> <code>{{ (selectedBlock.predecessors || []).join(', ') || '-' }}</code></p>
                    <p><b>Successors:</b> <code>{{ (selectedBlock.successors || []).join(', ') || '-' }}</code></p>
                </div>

                <h4>CFG View</h4>
                <div class="cfg-wrap" v-if="cfgNodes.length">
                    <div class="cfg-controls">
                        <button class="tiny-btn" @click="zoomOut">-</button>
                        <button class="tiny-btn" @click="zoomIn">+</button>
                        <button class="tiny-btn" @click="resetZoom">100%</button>
                        <button class="tiny-btn" @click="fitZoom">Fit</button>
                        <span class="muted">{{ Math.round(cfgZoom * 100) }}%</span>
                    </div>
                    <div class="cfg-canvas" ref="cfgWrap">
                    <svg
                        :viewBox="`0 0 ${cfgWidth} ${cfgHeight}`"
                        :width="Math.max(220, cfgWidth * cfgZoom)"
                        :height="Math.max(200, cfgHeight * cfgZoom)"
                        class="cfg-svg">
                        <defs>
                            <marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">
                                <path d="M0,0 L8,4 L0,8 z" fill="var(--edge)" />
                            </marker>
                        </defs>

                        <path
                            v-for="(edge, idx) in cfgRenderEdges"
                            :key="`e-${idx}`"
                            :d="edgePath(edge)"
                            class="cfg-edge"
                            :class="{ 'cfg-edge-in': edge.isIn, 'cfg-edge-out': edge.isOut }"
                            marker-end="url(#arrow)" />

                        <g
                            v-for="node in cfgNodes"
                            :key="node.address"
                            :transform="`translate(${node.x}, ${node.y})`"
                            class="cfg-node"
                            :class="{active: selectedKey === node.address}"
                            @click="selectBlock(node.address)">
                            <rect :width="nodeW" :height="nodeH" rx="8" ry="8" />
                            <text x="10" y="20">{{ node.address }}</text>
                            <text x="10" y="39">{{ node.instruction_count || 0 }} insn</text>
                        </g>
                    </svg>
                    </div>
                </div>
                <div v-else class="muted">No CFG edges found.</div>

                <h4>Instruction List</h4>
                <div class="insn-toolbar" v-if="selectedBlock">
                    <input
                        v-model.trim="instructionQuery"
                        class="insn-search"
                        placeholder="Search VIP / mnemonic / text"
                        @keydown.enter.prevent="jumpToNextInstruction" />
                    <select v-model="searchPriority" class="tiny-select">
                        <option value="vip">VIP first</option>
                        <option value="mnemonic">Mnemonic first</option>
                        <option value="text">Text first</option>
                    </select>
                    <button class="tiny-btn" @click="jumpToPrevInstruction">Prev</button>
                    <button class="tiny-btn" @click="jumpToNextInstruction">Next</button>
                    <button class="tiny-btn" @click="showPseudoPanel = !showPseudoPanel">{{ showPseudoPanel ? 'Hide' : 'Show' }} ASM</button>
                    <span class="muted">{{ filteredInstructions.length }} match</span>
                </div>

                <div class="asm-panel" v-if="showPseudoPanel && activeInstruction">
                    <div class="asm-title">Pseudo ASM</div>
                    <code class="asm-code">{{ toPseudoAsm(activeInstruction) }}</code>
                </div>

                <div class="insn-table-wrap" v-if="selectedBlock && shownInstructions.length">
                    <table class="insn-table">
                        <thead>
                            <tr>
                                <th></th>
                                <th>#</th>
                                <th>VIP</th>
                                <th>Mnemonic</th>
                                <th>Ops</th>
                                <th>Instruction</th>
                            </tr>
                        </thead>
                        <tbody>
                            <template v-for="insn in shownInstructions">
                                <tr
                                    :key="`row-${insn.index}`"
                                    :data-insn-index="insn.index"
                                    :class="{ 'insn-active': activeInstructionIndex === insn.index }"
                                    @click="activeInstructionIndex = insn.index">
                                    <td>
                                        <button class="expand-btn" @click.stop="toggleExpanded(insn.index)">
                                            {{ isExpanded(insn.index) ? '-' : '+' }}
                                        </button>
                                    </td>
                                    <td>{{ insn.index }}</td>
                                    <td><code>{{ insn.vip }}</code></td>
                                    <td><code>{{ (insn.display_mnemonic || insn.mnemonic || '').toUpperCase() }}</code></td>
                                    <td>{{ insn.operand_count }}</td>
                                    <td><code class="insn-text">{{ insn.display_text || insn.text }}</code></td>
                                </tr>
                                <tr v-if="isExpanded(insn.index)" :key="`detail-${insn.index}`" class="insn-detail-row">
                                    <td colspan="6">
                                        <div class="insn-detail-grid">
                                            <div>
                                                <div class="insn-detail-title">Descriptor</div>
                                                <div class="chip-row">
                                                    <span class="chip">name: {{ insn.desc ? insn.desc.name : '-' }}</span>
                                                    <span class="chip">volatile: {{ insn.desc && insn.desc.is_volatile ? 'yes' : 'no' }}</span>
                                                    <span class="chip">mem_write: {{ insn.desc && insn.desc.memory_write ? 'yes' : 'no' }}</span>
                                                    <span class="chip">access_size_idx: {{ insn.desc ? insn.desc.access_size_index : '-' }}</span>
                                                    <span class="chip">mem_op_idx: {{ insn.desc ? insn.desc.memory_operand_index : '-' }}</span>
                                                </div>
                                                <div class="chip-row">
                                                    <span class="chip">operand_types: {{ toTextList(insn.desc ? insn.desc.operand_types : []) }}</span>
                                                </div>
                                                <div class="chip-row">
                                                    <span class="chip">branch_vip: {{ toTextList(insn.desc ? insn.desc.branch_operands_vip : []) }}</span>
                                                    <span class="chip">branch_rip: {{ toTextList(insn.desc ? insn.desc.branch_operands_rip : []) }}</span>
                                                </div>
                                            </div>
                                            <div>
                                                <div class="insn-detail-title">Operands</div>
                                                <table class="operand-table">
                                                    <thead>
                                                        <tr>
                                                            <th>#</th>
                                                            <th>Type</th>
                                                            <th>Kind</th>
                                                            <th>Bits</th>
                                                            <th>Text</th>
                                                        </tr>
                                                    </thead>
                                                    <tbody>
                                                        <tr v-for="(op, opIndex) in (insn.operands || [])" :key="`op-${insn.index}-${opIndex}`">
                                                            <td>{{ opIndex }}</td>
                                                            <td><code>{{ op.type }}</code></td>
                                                            <td>{{ op.kind }}</td>
                                                            <td>{{ op.bit_count }}</td>
                                                            <td><code>{{ op.text }}</code></td>
                                                        </tr>
                                                    </tbody>
                                                </table>
                                            </div>
                                        </div>
                                    </td>
                                </tr>
                            </template>
                        </tbody>
                    </table>
                </div>
                <div v-else class="muted">No instruction details for current selection.</div>
            </div>

            <div class="resizer" v-if="i !== 1" :data-resizer-for="i - 1" @mousedown="mouseDown"></div>
        </div>
    </div>
</template>

<script>
import Vue from 'vue'

export default {
    props: {
        routine: {
            type: Object,
            required: true
        }
    },
    data() {
        return {
            count: 2,
            sizes: [45, 55],
            resizeIdx: 0,
            selectedKey: null,
            instructionQuery: '',
            searchPriority: 'vip',
            showPseudoPanel: true,
            activeInstructionIndex: null,
            expandedInstructions: {},
            currentMatchPos: -1,
            cfgZoom: 1,
            nodeW: 190,
            nodeH: 50,
            xGap: 70,
            yGap: 34,
            cfgPadding: 18
        }
    },
    methods: {
        mouseDown(evt) {
            this.resizeIdx = evt.target.getAttribute('data-resizer-for')
        },
        mouseUp() {
            if (this.dragging) this.resizeIdx = 0
        },
        calculateResize(idx, newPos) {
            const maxWidth = this.$refs.container.clientWidth
            const ratio = newPos / maxWidth
            let pct = ratio * 100
            if (pct < 15) pct = 15
            if (pct > 85) pct = 85
            this.setSize(idx, pct)
            this.setSize(idx + 1, 100 - pct)
        },
        setSize(idx, newSize) {
            Vue.set(this.sizes, idx, newSize)
        },
        mouseMove(evt) {
            if (this.dragging) this.calculateResize(this.resizeIdx - 1, evt.clientX)
        },
        ensureSizesEqual100() {
            let sum = 0
            for (const i in this.sizes) sum += this.sizes[i]
            if (sum < 100) Vue.set(this.sizes, 0, this.sizes[0] + (100 - sum))
        },
        selectBlock(key) {
            this.selectedKey = key
            this.activeInstructionIndex = null
            this.currentMatchPos = -1
            this.cfgZoom = 1
            this.fitZoom()
        },
        nodeAnchor(node, side) {
            if (side === 'left') return { x: node.x, y: node.y + this.nodeH / 2 }
            return { x: node.x + this.nodeW, y: node.y + this.nodeH / 2 }
        },
        edgePath(edge) {
            const dx = Math.max(20, Math.abs(edge.x2 - edge.x1) * 0.45)
            const c1x = edge.x1 + dx
            const c2x = edge.x2 - dx
            return `M ${edge.x1} ${edge.y1} C ${c1x} ${edge.y1}, ${c2x} ${edge.y2}, ${edge.x2} ${edge.y2}`
        },
        toPseudoAsm(insn) {
            const raw = (insn.display_text || insn.text || '').trim()
            const mnemonic = (insn.display_mnemonic || insn.mnemonic || '').toUpperCase()
            const escapedMnemonic = (insn.mnemonic || '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
            let operands = raw
            if (escapedMnemonic) {
                const prefix = new RegExp(`^${escapedMnemonic}\\s*`, 'i')
                operands = operands.replace(prefix, '')
            }
            operands = operands
                .replace(/\bsp\b/gi, 'rsp')
                .replace(/\bvr(\d+)\b/gi, 'v$1')
                .replace(/\s+/g, ' ')
                .trim()
            return operands ? `${mnemonic} ${operands}` : mnemonic
        },
        instructionMatches(insn) {
            const q = this.instructionQuery.toLowerCase()
            if (!q) return true
            return (
                (insn.vip || '').toLowerCase().includes(q) ||
                (insn.display_mnemonic || insn.mnemonic || '').toLowerCase().includes(q) ||
                (insn.display_text || insn.text || '').toLowerCase().includes(q)
            )
        },
        fieldMatchLevel(value, query) {
            if (!query) return 0
            const normalized = (value || '').toLowerCase()
            if (!normalized) return 99
            if (normalized === query) return 0
            if (normalized.startsWith(query)) return 1
            if (normalized.includes(query)) return 2
            return 99
        },
        scoreInstruction(insn) {
            const q = this.instructionQuery.toLowerCase()
            const levels = {
                vip: this.fieldMatchLevel(insn.vip, q),
                mnemonic: this.fieldMatchLevel(insn.display_mnemonic || insn.mnemonic, q),
                text: this.fieldMatchLevel(insn.display_text || insn.text, q)
            }
            const orders = {
                vip: ['vip', 'mnemonic', 'text'],
                mnemonic: ['mnemonic', 'vip', 'text'],
                text: ['text', 'mnemonic', 'vip']
            }
            const order = orders[this.searchPriority] || orders.vip
            return [levels[order[0]], levels[order[1]], levels[order[2]], insn.index]
        },
        jumpToInstructionAt(pos) {
            if (!this.filteredInstructions.length) return
            const idx = (pos + this.filteredInstructions.length) % this.filteredInstructions.length
            this.currentMatchPos = idx
            const target = this.filteredInstructions[idx]
            this.activeInstructionIndex = target.index
            this.$nextTick(() => {
                const row = this.$el.querySelector(`tr[data-insn-index='${target.index}']`)
                if (row) row.scrollIntoView({ block: 'nearest' })
            })
        },
        jumpToNextInstruction() {
            this.jumpToInstructionAt(this.currentMatchPos + 1)
        },
        jumpToPrevInstruction() {
            this.jumpToInstructionAt(this.currentMatchPos - 1)
        },
        toggleExpanded(index) {
            const next = { ...this.expandedInstructions }
            next[index] = !next[index]
            this.expandedInstructions = next
        },
        isExpanded(index) {
            return !!this.expandedInstructions[index]
        },
        toTextList(values) {
            if (!values || !values.length) return '-'
            return values.join(', ')
        },
        zoomIn() {
            this.cfgZoom = Math.min(2.5, +(this.cfgZoom + 0.15).toFixed(2))
        },
        zoomOut() {
            this.cfgZoom = Math.max(0.35, +(this.cfgZoom - 0.15).toFixed(2))
        },
        resetZoom() {
            this.cfgZoom = 1
        },
        fitZoom() {
            this.$nextTick(() => {
                const wrap = this.$refs.cfgWrap
                if (!wrap || !this.cfgWidth) return
                const fit = wrap.clientWidth / this.cfgWidth
                this.cfgZoom = Math.max(0.35, Math.min(2.5, +fit.toFixed(2)))
            })
        }
    },
    computed: {
        dragging() {
            return this.resizeIdx !== 0
        },
        blockRows() {
            return this.routine.blocks || []
        },
        selectedBlock() {
            return this.blockRows.find((x) => x.address === this.selectedKey) || this.blockRows[0] || null
        },
        filteredInstructions() {
            const list = this.selectedBlock ? (this.selectedBlock.instructions || []) : []
            const matches = list.filter((insn) => this.instructionMatches(insn))
            if (!this.instructionQuery) return matches

            const scored = matches.map((insn) => ({ insn, score: this.scoreInstruction(insn) }))
            scored.sort((a, b) => {
                for (let i = 0; i < a.score.length; i += 1) {
                    if (a.score[i] !== b.score[i]) return a.score[i] - b.score[i]
                }
                return 0
            })
            return scored.map((x) => x.insn)
        },
        shownInstructions() {
            return this.filteredInstructions
        },
        activeInstruction() {
            if (!this.selectedBlock) return null
            const all = this.selectedBlock.instructions || []
            if (this.activeInstructionIndex === null || this.activeInstructionIndex === undefined) {
                return this.shownInstructions[0] || all[0] || null
            }
            return all.find((x) => x.index === this.activeInstructionIndex) || null
        },
        entryAddress() {
            return this.routine.entry_point || (this.blockRows[0] ? this.blockRows[0].address : null)
        },
        levelMap() {
            const levels = {}
            if (!this.blockRows.length) return levels
            const adjacency = {}
            for (const blk of this.blockRows) adjacency[blk.address] = blk.successors || []

            const start = this.entryAddress
            const queue = []
            if (start) {
                levels[start] = 0
                queue.push(start)
            }

            while (queue.length) {
                const cur = queue.shift()
                const curLevel = levels[cur]
                for (const nxt of (adjacency[cur] || [])) {
                    if (levels[nxt] !== undefined) continue
                    levels[nxt] = curLevel + 1
                    queue.push(nxt)
                }
            }

            let tail = Math.max(0, ...Object.values(levels))
            for (const blk of this.blockRows) {
                if (levels[blk.address] === undefined) {
                    tail += 1
                    levels[blk.address] = tail
                }
            }
            return levels
        },
        nodesByLevel() {
            const grouped = {}
            for (const blk of this.blockRows) {
                const lvl = this.levelMap[blk.address] || 0
                if (!grouped[lvl]) grouped[lvl] = []
                grouped[lvl].push(blk)
            }
            const ordered = []
            for (const key of Object.keys(grouped).map(Number).sort((a, b) => a - b)) {
                ordered.push(grouped[key])
            }
            return ordered
        },
        cfgNodes() {
            const nodes = []
            this.nodesByLevel.forEach((levelNodes, level) => {
                levelNodes.forEach((blk, rowIndex) => {
                    nodes.push({
                        ...blk,
                        x: this.cfgPadding + level * (this.nodeW + this.xGap),
                        y: this.cfgPadding + rowIndex * (this.nodeH + this.yGap)
                    })
                })
            })
            return nodes
        },
        cfgNodeMap() {
            const map = {}
            for (const n of this.cfgNodes) map[n.address] = n
            return map
        },
        cfgRenderEdges() {
            const out = []
            for (const e of (this.routine.cfg_edges || [])) {
                const src = this.cfgNodeMap[e.from]
                const dst = this.cfgNodeMap[e.to]
                if (!src || !dst) continue
                const from = this.nodeAnchor(src, 'right')
                const to = this.nodeAnchor(dst, 'left')
                out.push({
                    x1: from.x,
                    y1: from.y,
                    x2: to.x,
                    y2: to.y,
                    isOut: this.selectedKey ? this.selectedKey === e.from : false,
                    isIn: this.selectedKey ? this.selectedKey === e.to : false
                })
            }
            return out
        },
        cfgWidth() {
            const levelCount = Math.max(this.nodesByLevel.length, 1)
            return this.cfgPadding * 2 + levelCount * this.nodeW + (levelCount - 1) * this.xGap
        },
        cfgHeight() {
            const maxRows = Math.max(1, ...this.nodesByLevel.map((group) => group.length || 1))
            return this.cfgPadding * 2 + maxRows * this.nodeH + (maxRows - 1) * this.yGap
        }
    },
    watch: {
        blockRows() {
            const hasSelected = this.blockRows.some((x) => x.address === this.selectedKey)
            if (!hasSelected) {
                if (this.blockRows.length) this.selectBlock(this.blockRows[0].address)
                else this.selectedKey = null
            }
        },
        instructionQuery() {
            this.currentMatchPos = -1
            this.activeInstructionIndex = null
        },
        searchPriority() {
            this.currentMatchPos = -1
            this.activeInstructionIndex = null
        }
    },
    mounted() {
        this.ensureSizesEqual100()
        this.$refs.container.addEventListener('mousemove', this.mouseMove)
    },
    created() {
        window.addEventListener('mouseup', this.mouseUp)
    },
    destroyed() {
        window.removeEventListener('mouseup', this.mouseUp)
    }
}
</script>

<style scoped>
.container {
    margin: 0;
    height: 100%;
    width: 100%;
    padding: 0;
    overflow: hidden;
}
.dragging {
    user-select: none;
}
.horizontal > .splittable {
    display: block;
}
.horizontal {
    display: flex;
    height: 100%;
}
.splittable {
    height: 100%;
    position: relative;
    vertical-align: top;
    overflow: auto;
}
.pane-content {
    padding: 0.75em;
    font-size: 13px;
    color: var(--text);
}
h3, h4 {
    margin: 0.35em 0 0.55em;
}
p {
    margin: 0.32em 0;
}
.row {
    display: flex;
    justify-content: space-between;
    padding: 0.35em 0.45em;
    border-radius: 6px;
    cursor: pointer;
    color: var(--text);
}
.row:hover {
    background: var(--panel-2);
}
.row.selected {
    background: var(--accent-soft);
    color: var(--text);
}
.cfg-wrap {
    border: 1px solid var(--panel-border);
    border-radius: 8px;
    max-height: 36vh;
    overflow: auto;
    margin-bottom: 0.6em;
    background: var(--panel);
}
.cfg-controls {
    display: flex;
    gap: 0.35em;
    align-items: center;
    padding: 0.35em;
    border-bottom: 1px solid var(--panel-border);
    background: var(--panel-2);
}
.cfg-canvas {
    overflow: auto;
    max-height: 32vh;
}
.cfg-svg {
    display: block;
    min-height: 220px;
}
.cfg-edge {
    stroke: var(--edge);
    stroke-width: 1.4;
    opacity: 0.38;
    fill: none;
}
.cfg-edge-out {
    stroke: #22c55e;
    opacity: 1;
    stroke-width: 2.1;
}
.cfg-edge-in {
    stroke: #f97316;
    opacity: 1;
    stroke-width: 2.1;
}
.cfg-node {
    cursor: pointer;
}
.cfg-node rect {
    fill: var(--panel-2);
    stroke: var(--panel-border);
}
.cfg-node.active rect {
    fill: var(--accent-soft);
    stroke: var(--accent);
    stroke-width: 1.6;
}
.cfg-node text {
    font-size: 12px;
    fill: var(--text);
    font-family: Consolas, monospace;
}
.insn-toolbar {
    display: flex;
    gap: 0.45em;
    align-items: center;
    margin-bottom: 0.45em;
}
.insn-search {
    flex: 1;
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    background: var(--panel-2);
    color: var(--text);
    padding: 0.28em 0.5em;
}
.tiny-btn {
    border: 1px solid var(--panel-border);
    border-radius: 5px;
    background: var(--panel-2);
    color: var(--text);
    padding: 0.24em 0.5em;
    cursor: pointer;
}
.tiny-select {
    border: 1px solid var(--panel-border);
    border-radius: 5px;
    background: var(--panel-2);
    color: var(--text);
    padding: 0.24em 0.45em;
}
.asm-panel {
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    background: var(--panel-2);
    padding: 0.45em 0.55em;
    margin-bottom: 0.45em;
}
.asm-title {
    font-size: 12px;
    color: var(--muted);
    margin-bottom: 0.2em;
}
.asm-code {
    color: var(--text);
    white-space: pre-wrap;
}
.insn-table-wrap {
    max-height: 36vh;
    overflow: auto;
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    background: var(--panel);
}
.insn-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 12px;
}
.insn-table thead th {
    position: sticky;
    top: 0;
    background: var(--panel-2);
    border-bottom: 1px solid var(--panel-border);
    text-align: left;
    padding: 0.35em 0.45em;
    color: var(--muted);
}
.expand-btn {
    border: 1px solid var(--panel-border);
    background: var(--panel-2);
    color: var(--text);
    border-radius: 4px;
    width: 1.8em;
    height: 1.8em;
    line-height: 1.4em;
    cursor: pointer;
}
.insn-detail-row td {
    background: var(--panel-2);
}
.insn-detail-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.6em;
}
.insn-detail-title {
    color: var(--muted);
    margin-bottom: 0.25em;
    font-size: 12px;
}
.chip-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35em;
    margin-bottom: 0.3em;
}
.chip {
    border: 1px solid var(--panel-border);
    border-radius: 999px;
    padding: 0.12em 0.5em;
    font-size: 11px;
}
.operand-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 11px;
}
.operand-table th,
.operand-table td {
    border-bottom: 1px solid var(--panel-border);
    text-align: left;
    padding: 0.22em 0.35em;
}
.insn-table td {
    border-bottom: 1px solid var(--panel-border);
    padding: 0.3em 0.45em;
    vertical-align: top;
    color: var(--text);
}
.insn-active {
    background: var(--accent-soft);
}
.insn-text {
    white-space: pre;
}
.muted {
    color: var(--muted);
}
.resizer {
    position: absolute;
    left: -3px;
    height: 100%;
    width: 6px;
    z-index: 2;
    cursor: e-resize;
}

@media (max-width: 980px) {
    .insn-toolbar {
        flex-wrap: wrap;
    }
    .tiny-select,
    .tiny-btn {
        font-size: 12px;
    }
    .cfg-wrap {
        max-height: 42vh;
    }
    .insn-detail-grid {
        grid-template-columns: 1fr;
    }
}
</style>