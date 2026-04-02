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
                    <button class="tiny-btn" @click="promptEditImmediate">Edit immediate</button>
                    <button class="tiny-btn" @click="openInstructionEditor">Edit instruction</button>
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

            <div class="resizer" v-if="i !== 1" :data-resizer-for="i - 1" @mousedown.stop.prevent="mouseDown"></div>
        </div>

        <div v-if="instructionEditor.open" class="modal-overlay" @click.self="closeInstructionEditor">
            <div class="modal-card">
                <h4>Instruction Editor</h4>
                <p class="muted">Strict VTIL parsing with schema-guided autocomplete.</p>

                <label class="modal-label">Mnemonic</label>
                <input
                    v-model.trim="instructionEditor.mnemonic"
                    ref="mnemonicInput"
                    class="insn-search"
                    @focus="onMnemonicFocus"
                    @input="onMnemonicInput"
                    @keydown="onFieldKeyDown($event, 'mnemonic')"
                    placeholder="e.g. mov" />
                <div v-if="showSuggestionsFor('mnemonic')" class="suggestion-box">
                    <button
                        v-for="(entry, idx) in activeSuggestions"
                        :key="`m-s-${entry}-${idx}`"
                        class="suggestion-item"
                        :class="{ active: idx === suggestionState.highlighted }"
                        @mousedown.prevent="commitSuggestion('mnemonic', entry)">
                        {{ entry }}
                    </button>
                </div>
                <div v-if="mnemonicValidationError" class="field-error">{{ mnemonicValidationError }}</div>

                <div class="chip-row" v-if="activeInstructionSchema">
                    <span class="chip">operand_types: {{ (activeInstructionSchema.operand_types || []).join(', ') || '-' }}</span>
                    <span class="chip">volatile: {{ activeInstructionSchema.is_volatile ? 'yes' : 'no' }}</span>
                    <span class="chip">mem_write: {{ activeInstructionSchema.memory_write ? 'yes' : 'no' }}</span>
                </div>
                <div class="chip-row" v-if="activeInstructionSchema && activeInstructionSchema.example_line">
                    <span class="chip">example: {{ activeInstructionSchema.example_line }}</span>
                </div>

                <label class="modal-label">Operands</label>
                <div v-if="expectedOperandTypes.length === 0" class="muted">No operands for this instruction.</div>
                <div v-else>
                    <div v-for="(opType, opIndex) in expectedOperandTypes" :key="`ed-op-${opIndex}`" class="operand-edit-row">
                        <span class="op-type">{{ opType }}</span>
                        <input
                            v-model.trim="instructionEditor.operands[opIndex]"
                            class="insn-search"
                            :ref="`operandInput-${opIndex}`"
                            @focus="onOperandFocus(opIndex, opType)"
                            @input="onOperandInput(opIndex, opType)"
                            @keydown="onFieldKeyDown($event, `operand:${opIndex}`)"
                            :placeholder="operandPlaceholder(opType)" />
                        <div v-if="showSuggestionsFor(`operand:${opIndex}`)" class="suggestion-box">
                            <button
                                v-for="(entry, idx) in activeSuggestions"
                                :key="`o-s-${opIndex}-${entry}-${idx}`"
                                class="suggestion-item"
                                :class="{ active: idx === suggestionState.highlighted }"
                                @mousedown.prevent="commitSuggestion(`operand:${opIndex}`, entry)">
                                {{ entry }}
                            </button>
                        </div>
                        <div class="operand-hint">{{ operandHint(opType, opIndex) }}</div>
                        <div v-if="operandValidationErrors[opIndex]" class="field-error">{{ operandValidationErrors[opIndex] }}</div>
                    </div>
                </div>

                <label class="modal-label">Preview</label>
                <code class="asm-code">{{ instructionEditorPreview }}</code>
                <div v-if="instructionValidationError" class="field-error">{{ instructionValidationError }}</div>

                <div class="modal-actions">
                    <button class="tiny-btn" @click="closeInstructionEditor">Cancel</button>
                    <button class="tiny-btn" :disabled="!instructionEditorCanSubmit" @click="submitInstructionEditor">Apply</button>
                </div>
            </div>
        </div>

        <div v-if="immediateEditor.open" class="modal-overlay" @click.self="closeImmediateEditor">
            <div class="modal-card immediate-modal">
                <h4>Edit Immediate</h4>
                <p class="muted">Update a single immediate operand directly without rebuilding the instruction text.</p>

                <div class="chip-row" v-if="selectedBlock && activeInstruction">
                    <span class="chip">block: {{ selectedBlock.address }}</span>
                    <span class="chip">instruction: {{ immediateEditor.instruction }}</span>
                    <span class="chip">preview: {{ immediateEditorPreview || '-' }}</span>
                </div>

                <label class="modal-label">Immediate operand</label>
                <div v-if="immediateOperandOptions.length > 1">
                    <select v-model.number="immediateEditor.operand" class="tiny-select immediate-select" @change="syncImmediateEditorValue">
                        <option v-for="op in immediateOperandOptions" :key="`imm-${op.index}`" :value="op.index">
                            Operand {{ op.index }} · {{ op.text || op.kind }}
                        </option>
                    </select>
                </div>
                <div v-else class="muted immediate-single">
                    Operand {{ currentImmediateOperand ? currentImmediateOperand.index : '-' }} · {{ currentImmediateOperand ? (currentImmediateOperand.text || currentImmediateOperand.kind) : 'n/a' }}
                </div>

                <div class="chip-row" v-if="currentImmediateOperand">
                    <span class="chip">type: {{ currentImmediateOperand.type || 'immediate' }}</span>
                    <span class="chip">kind: {{ currentImmediateOperand.kind || 'immediate' }}</span>
                    <span class="chip">bits: {{ currentImmediateOperand.bit_count || '-' }}</span>
                    <span class="chip">current: {{ currentImmediateOperand.text || '-' }}</span>
                </div>

                <label class="modal-label">Value</label>
                <input
                    v-model.trim="immediateEditor.value"
                    ref="immediateValueInput"
                    class="insn-search"
                    placeholder="decimal, hex (0x...), or negative value"
                    @keydown.enter.prevent="submitImmediateEditor" />
                <div class="field-error" v-if="immediateEditorValidationError">{{ immediateEditorValidationError }}</div>

                <label class="modal-label">Preview</label>
                <code class="asm-code">{{ immediateEditorPreview || '-' }}</code>

                <div class="modal-actions">
                    <button class="tiny-btn" @click="closeImmediateEditor">Cancel</button>
                    <button class="tiny-btn" :disabled="!immediateEditorCanSubmit" @click="submitImmediateEditor">Apply</button>
                </div>
            </div>
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
        },
        editorSchema: {
            type: Object,
            default: () => ({
                mnemonics: [],
                registers: [],
                instructions: []
            })
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
            cfgPadding: 18,
            instructionEditor: {
                open: false,
                mnemonic: '',
                operands: []
            },
            immediateEditor: {
                open: false,
                block: '',
                instruction: null,
                operand: null,
                value: ''
            },
            suggestionState: {
                field: '',
                visible: false,
                highlighted: 0,
                items: []
            }
        }
    },
    methods: {
        mouseDown(evt) {
            const idx = Number.parseInt(evt.currentTarget.getAttribute('data-resizer-for'), 10)
            if (!Number.isInteger(idx)) return

            evt.preventDefault()
            evt.stopPropagation()
            this.resizeIdx = idx
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
            if (!this.dragging) return
            if (evt.buttons === 0) {
                this.resizeIdx = 0
                return
            }

            evt.preventDefault()
            this.calculateResize(this.resizeIdx - 1, evt.clientX)
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
        promptEditImmediate() {
            this.openImmediateEditor()
        },
        openImmediateEditor() {
            if (!this.selectedBlock || !this.activeInstruction) {
                window.alert('Select a block and instruction first.')
                return
            }

            const immediateOperands = this.immediateOperandOptions

            if (!immediateOperands.length) {
                window.alert('Selected instruction has no immediate operand.')
                return
            }

            const selectedOperand = immediateOperands[0]
            this.immediateEditor = {
                open: true,
                block: this.selectedBlock.address,
                instruction: this.activeInstruction.index,
                operand: selectedOperand.index,
                value: this.initialImmediateValue(selectedOperand)
            }
            this.$nextTick(() => {
                const target = this.$refs.immediateValueInput
                if (target && target.focus) target.focus()
            })
        },
        initialImmediateValue(op) {
            if (!op) return '0'
            if (op.immediate && op.immediate.i64 !== undefined && op.immediate.i64 !== null) return String(op.immediate.i64)
            if (op.immediate && op.immediate.u64 !== undefined && op.immediate.u64 !== null) return String(op.immediate.u64)
            if (op.text !== undefined && op.text !== null && String(op.text).trim()) return String(op.text).trim()
            return '0'
        },
        syncImmediateEditorValue() {
            const current = this.currentImmediateOperand
            if (current) this.immediateEditor.value = this.initialImmediateValue(current)
        },
        closeImmediateEditor() {
            this.immediateEditor.open = false
        },
        submitImmediateEditor() {
            if (!this.selectedBlock || !this.activeInstruction) return

            if (!this.immediateEditorCanSubmit) {
                window.alert(this.immediateEditorValidationError || 'Please fix immediate validation issues first.')
                return
            }

            this.$emit('edit-immediate', {
                block: this.selectedBlock.address,
                instruction: this.activeInstruction.index,
                operand: this.immediateEditor.operand,
                value: String(this.immediateEditor.value || '').trim()
            })
            this.closeImmediateEditor()
        },
        openInstructionEditor() {
            if (!this.selectedBlock || !this.activeInstruction) {
                window.alert('Select a block and instruction first.')
                return
            }

            const source = this.activeInstruction
            const tokenized = this.tokenizeInstructionText(source.display_text || source.text || '')
            const mnemonic = (tokenized.mnemonic || source.mnemonic || source.display_mnemonic || '').toLowerCase()
            let operands = (tokenized.operands || []).map((x) => String(x || '').trim())
            if (!operands.length) {
                operands = (source.display_operands || []).map((x) => String(x || '').trim())
            }
            if (!operands.length) {
                operands = (source.operands || []).map((x) => String((x && x.text) || '').trim())
            }

            this.instructionEditor = {
                open: true,
                mnemonic,
                operands
            }
            this.suggestionState.visible = false
            this.syncEditorOperandCount()
        },
        closeInstructionEditor() {
            this.instructionEditor.open = false
            this.suggestionState.visible = false
        },
        syncEditorOperandCount() {
            const need = this.expectedOperandTypes.length
            const next = (this.instructionEditor.operands || []).slice(0, need)
            while (next.length < need) next.push('')
            this.instructionEditor.operands = next
        },
        tokenizeInstructionText(text) {
            const raw = String(text || '').trim()
            if (!raw) return { mnemonic: '', operands: [] }
            const parts = raw.split(/\s+/)
            const mnemonic = (parts.shift() || '').toLowerCase()
            const tail = raw.slice(raw.indexOf(mnemonic) + mnemonic.length).trim()
            if (!tail) return { mnemonic, operands: [] }
            const ops = tail.split(',').map((x) => x.trim()).filter(Boolean)
            return { mnemonic, operands: ops }
        },
        operandPlaceholder(opType) {
            if (opType === 'read_imm') return '0x20:64'
            if (opType === 'read_reg' || opType === 'write' || opType === 'readwrite') return 'register name'
            return 'register or immediate'
        },
        operandSuggestions(opType) {
            if (opType === 'read_reg' || opType === 'write' || opType === 'readwrite') {
                return this.editorRegisterList
            }
            if (opType === 'read_imm') {
                return ['0x10:64', '0x20:64', '1:1', '0:1']
            }
            return [...this.editorRegisterList.slice(0, 24), '0x10:64', '0x20:64', '1:1', '0:1']
        },
        operandExampleList(opType) {
            if (opType === 'read_reg' || opType === 'write' || opType === 'readwrite') {
                return this.editorRegisterList.slice(0, 3)
            }
            if (opType === 'read_imm') {
                return ['0x10:64', '0x20:64', '1:1']
            }
            return [...this.editorRegisterList.slice(0, 2), '0x10:64', '0x20:64']
        },
        operandHint(opType, index) {
            const examples = this.operandExampleList(opType, index)
            const accepted = examples.length ? examples.join(', ') : 'n/a'
            return `Expected: ${opType}. Accepted: ${accepted}.`
        },
        normalizeForCompare(value) {
            return String(value || '').trim().toLowerCase()
        },
        isKnownRegister(token) {
            const normalized = this.normalizeForCompare(token)
            if (!normalized) return false
            return this.editorRegisterList.some((x) => this.normalizeForCompare(x) === normalized)
        },
        isImmediateToken(token, requireBits) {
            const t = String(token || '').trim().toLowerCase()
            if (!t) return false
            const rNoBits = /^-?(0x[0-9a-f]+|\d+)$/i
            const rWithBits = /^-?(0x[0-9a-f]+|\d+):(\d+)$/i
            if (requireBits) {
                const m = t.match(rWithBits)
                if (!m) return false
                const bits = Number.parseInt(m[2], 10)
                return Number.isInteger(bits) && bits > 0 && bits <= 64
            }
            if (rNoBits.test(t)) return true
            const m = t.match(rWithBits)
            if (!m) return false
            const bits = Number.parseInt(m[2], 10)
            return Number.isInteger(bits) && bits > 0 && bits <= 64
        },
        validateOperand(opType, token) {
            const t = String(token || '').trim()
            if (!t) return 'Operand is required.'
            if (opType === 'read_reg' || opType === 'write' || opType === 'readwrite') {
                return this.isKnownRegister(t) ? '' : 'Must match a known VTIL register name.'
            }
            if (opType === 'read_imm') {
                return this.isImmediateToken(t, true) ? '' : 'Immediate must be value:bits, e.g. 0x20:64.'
            }
            if (opType === 'read_any') {
                if (this.isKnownRegister(t) || this.isImmediateToken(t, false)) return ''
                return 'Must be a known register or an immediate.'
            }
            return 'Unsupported operand type.'
        },
        refreshSuggestions(field, query, pool) {
            const q = this.normalizeForCompare(query)
            const unique = Array.from(new Set((pool || []).filter(Boolean)))
            const starts = unique.filter((x) => this.normalizeForCompare(x).startsWith(q))
            const contains = unique.filter((x) => !starts.includes(x) && this.normalizeForCompare(x).includes(q))
            const items = [...starts, ...contains].slice(0, 12)
            this.suggestionState = {
                field,
                visible: items.length > 0,
                highlighted: 0,
                items
            }
        },
        showSuggestionsFor(field) {
            return this.suggestionState.visible && this.suggestionState.field === field && this.activeSuggestions.length > 0
        },
        onMnemonicFocus() {
            this.refreshSuggestions('mnemonic', this.instructionEditor.mnemonic, this.editorMnemonicList)
        },
        onMnemonicInput() {
            this.refreshSuggestions('mnemonic', this.instructionEditor.mnemonic, this.editorMnemonicList)
        },
        onOperandFocus(index, opType) {
            this.refreshSuggestions(`operand:${index}`, this.instructionEditor.operands[index], this.operandSuggestions(opType))
        },
        onOperandInput(index, opType) {
            this.refreshSuggestions(`operand:${index}`, this.instructionEditor.operands[index], this.operandSuggestions(opType))
        },
        commitSuggestion(field, value) {
            if (field === 'mnemonic') {
                this.instructionEditor.mnemonic = value
            } else if (field.startsWith('operand:')) {
                const idx = Number.parseInt(field.split(':')[1], 10)
                if (Number.isInteger(idx)) this.$set(this.instructionEditor.operands, idx, value)
            }
            this.suggestionState.visible = false
        },
        focusNextEditorField(field) {
            if (field === 'mnemonic') {
                if (this.expectedOperandTypes.length > 0) {
                    const target = this.$refs['operandInput-0']
                    if (target && target.focus) target.focus()
                }
                return
            }
            if (field.startsWith('operand:')) {
                const idx = Number.parseInt(field.split(':')[1], 10)
                if (!Number.isInteger(idx)) return
                const next = idx + 1
                if (next < this.expectedOperandTypes.length) {
                    const target = this.$refs[`operandInput-${next}`]
                    if (target && target.focus) target.focus()
                }
            }
        },
        onFieldKeyDown(event, field) {
            if (!this.showSuggestionsFor(field)) {
                if (event.key === 'Enter' && field !== 'mnemonic' && this.instructionEditorCanSubmit) {
                    event.preventDefault()
                    this.submitInstructionEditor()
                }
                return
            }

            if (event.key === 'ArrowDown') {
                event.preventDefault()
                this.suggestionState.highlighted = (this.suggestionState.highlighted + 1) % this.activeSuggestions.length
                return
            }

            if (event.key === 'ArrowUp') {
                event.preventDefault()
                this.suggestionState.highlighted = (this.suggestionState.highlighted + this.activeSuggestions.length - 1) % this.activeSuggestions.length
                return
            }

            if (event.key === 'Enter' || event.key === 'Tab') {
                event.preventDefault()
                const pick = this.activeSuggestions[this.suggestionState.highlighted]
                if (pick) this.commitSuggestion(field, pick)
                this.focusNextEditorField(field)
                return
            }

            if (event.key === 'Escape') {
                this.suggestionState.visible = false
            }
        },
        submitInstructionEditor() {
            if (!this.selectedBlock || !this.activeInstruction) return

            if (!this.instructionEditorCanSubmit) {
                window.alert(this.instructionValidationError || 'Please fix instruction validation issues first.')
                return
            }

            const mnemonic = (this.instructionEditor.mnemonic || '').trim().toLowerCase()

            const operands = (this.instructionEditor.operands || []).map((x) => String(x || '').trim()).filter((x, idx) => {
                if (idx < this.expectedOperandTypes.length) return true
                return x.length > 0
            })

            if (operands.some((x) => !x)) {
                window.alert('All operand fields must be filled.')
                return
            }

            const text = operands.length ? `${mnemonic} ${operands.join(', ')}` : mnemonic
            this.$emit('edit-instruction', {
                block: this.selectedBlock.address,
                instruction: this.activeInstruction.index,
                text
            })
            this.closeInstructionEditor()
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
        },
        editorMnemonicList() {
            return (this.editorSchema && this.editorSchema.mnemonics) ? this.editorSchema.mnemonics : []
        },
        editorRegisterList() {
            return (this.editorSchema && this.editorSchema.registers) ? this.editorSchema.registers : []
        },
        editorInstructionRows() {
            return (this.editorSchema && this.editorSchema.instructions) ? this.editorSchema.instructions : []
        },
        editorInstructionMap() {
            const map = {}
            for (const row of this.editorInstructionRows) {
                if (row && row.name) map[String(row.name).toLowerCase()] = row
            }
            return map
        },
        immediateOperandOptions() {
            if (!this.activeInstruction || !Array.isArray(this.activeInstruction.operands)) return []
            return this.activeInstruction.operands
                .map((op, index) => ({ ...op, index }))
                .filter((op) => op.kind === 'immediate')
        },
        currentImmediateOperand() {
            if (!this.immediateOperandOptions.length) return null
            const targetIndex = this.immediateEditor.operand
            return this.immediateOperandOptions.find((op) => op.index === targetIndex) || this.immediateOperandOptions[0] || null
        },
        activeInstructionSchema() {
            const key = (this.instructionEditor.mnemonic || '').toLowerCase()
            return this.editorInstructionMap[key] || null
        },
        expectedOperandTypes() {
            return this.activeInstructionSchema ? (this.activeInstructionSchema.operand_types || []) : []
        },
        mnemonicValidationError() {
            const value = (this.instructionEditor.mnemonic || '').trim().toLowerCase()
            if (!value) return 'Mnemonic is required.'
            if (!this.editorInstructionMap[value]) return 'Unknown VTIL mnemonic.'
            return ''
        },
        operandValidationErrors() {
            return this.expectedOperandTypes.map((opType, idx) => {
                const token = (this.instructionEditor.operands || [])[idx] || ''
                return this.validateOperand(opType, token)
            })
        },
        instructionValidationError() {
            if (this.mnemonicValidationError) return this.mnemonicValidationError
            const first = this.operandValidationErrors.find((x) => !!x)
            return first || ''
        },
        instructionEditorCanSubmit() {
            if (!this.instructionEditor.open) return false
            if (this.mnemonicValidationError) return false
            return !this.operandValidationErrors.some((x) => !!x)
        },
        activeSuggestions() {
            return this.suggestionState.items || []
        },
        instructionEditorPreview() {
            const mnemonic = (this.instructionEditor.mnemonic || '').trim()
            if (!mnemonic) return ''
            const ops = (this.instructionEditor.operands || []).map((x) => String(x || '').trim()).filter(Boolean)
            return ops.length ? `${mnemonic} ${ops.join(', ')}` : mnemonic
        },
        immediateEditorValidationError() {
            const value = String(this.immediateEditor.value || '').trim()
            if (!this.immediateEditor.open) return ''
            if (!this.currentImmediateOperand) return 'Select an immediate operand first.'
            if (!value) return 'Immediate value is required.'
            return /^-?(0x[0-9a-f]+|\d+)$/i.test(value) ? '' : 'Use decimal, hex (0x...), or a negative value.'
        },
        immediateEditorCanSubmit() {
            return this.immediateEditor.open && !this.immediateEditorValidationError
        },
        immediateEditorPreview() {
            if (!this.immediateEditor.open || !this.currentImmediateOperand) return ''
            const value = String(this.immediateEditor.value || '').trim()
            if (!value) return ''
            return `operand ${this.currentImmediateOperand.index}: ${value}`
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
        },
        'instructionEditor.mnemonic'() {
            this.syncEditorOperandCount()
        },
        expectedOperandTypes() {
            this.syncEditorOperandCount()
        }
    },
    mounted() {
        this.ensureSizesEqual100()
        window.addEventListener('mousemove', this.mouseMove)
    },
    created() {
        window.addEventListener('mouseup', this.mouseUp)
    },
    beforeDestroy() {
        window.removeEventListener('mousemove', this.mouseMove)
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

.modal-overlay {
    position: fixed;
    inset: 0;
    background: rgba(2, 6, 23, 0.62);
    z-index: 300;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 1rem;
}

.modal-card {
    width: min(920px, 100%);
    max-height: 88vh;
    overflow: auto;
    border: 1px solid var(--panel-border);
    border-radius: 10px;
    background: var(--panel);
    padding: 0.8rem;
    box-shadow: 0 14px 42px rgba(0, 0, 0, 0.35);
}

.modal-label {
    display: block;
    margin: 0.4rem 0 0.25rem;
    color: var(--muted);
    font-size: 12px;
}

.field-error {
    color: #ef4444;
    font-size: 12px;
    margin-top: 0.2rem;
}

.suggestion-box {
    display: flex;
    flex-direction: column;
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    overflow: hidden;
    margin-top: 0.2rem;
    margin-bottom: 0.25rem;
    background: var(--panel-2);
}

.suggestion-item {
    border: 0;
    text-align: left;
    background: transparent;
    color: var(--text);
    cursor: pointer;
    padding: 0.32rem 0.46rem;
    font-size: 12px;
}

.suggestion-item:hover,
.suggestion-item.active {
    background: var(--accent-soft);
}

.operand-edit-row {
    display: grid;
    grid-template-columns: 140px 1fr;
    gap: 0.5rem;
    margin-bottom: 0.4rem;
    align-items: center;
}

.op-type {
    font-family: Consolas, monospace;
    font-size: 12px;
    color: var(--muted);
}

.operand-hint {
    grid-column: 2;
    color: var(--muted);
    font-size: 11px;
    margin-top: -0.1rem;
    margin-bottom: 0.15rem;
}

.modal-actions {
    display: flex;
    justify-content: flex-end;
    gap: 0.5rem;
    margin-top: 0.7rem;
}

.modal-actions .tiny-btn:disabled {
    opacity: 0.45;
    cursor: not-allowed;
}

.immediate-modal {
    width: min(760px, 100%);
}

.immediate-select {
    width: 100%;
}

.immediate-single {
    padding: 0.32rem 0.15rem 0.1rem;
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
    .operand-edit-row {
        grid-template-columns: 1fr;
    }
    .operand-hint {
        grid-column: 1;
    }

    .immediate-modal {
        width: min(920px, 100%);
    }
}
</style>