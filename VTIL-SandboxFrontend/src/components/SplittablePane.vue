<template>
    <div :class="{container: true, horizontal: true, dragging: dragging}" ref="container">
        <div
            class="splittable"
            v-for="i in count"
            :key="i"
            :data-id="i"
            v-bind:style="{ width: sizes[i - 1] + '%' }">
            
            <div class="resizer"
                v-if="i != 1"
                :data-resizer-for="i - 1"
                @mousedown="mouseDown">
            </div>
            
            
        </div>
    </div>
</template>
<script>
import Vue from 'vue'


export default {
    data() {
        return {
            count: 2,
            sizes: [50, 50],
            resizeIdx: 0
        };
    },
    methods: {
        mouseDown(evt) {
            this.resizeIdx = evt.target.getAttribute("data-resizer-for");
        },
        mouseUp() {
            if(this.dragging) {
                this.resizeIdx = 0;
            }
        },
        calculateResize(idx, newPos) {
            var maxWidth = this.$refs.container.clientWidth;
            var ratio = newPos / maxWidth;
            var pct = ratio * 100;

            if(pct < 10)
                pct = 10;
            if(pct > 90)
                pct = 90;

            this.setSize(idx, pct);
            this.setSize(idx + 1, 100 - pct);
        },
        setSize(idx, newSize) {
            Vue.set(this.sizes, idx, newSize);
        },
        mouseMove(evt) {
            if(this.dragging) {
                this.calculateResize(this.resizeIdx - 1, evt.clientX);
            }
        },
        ensureSizesEqual100() {
            var sum = 0;
            for(var i in this.sizes) {
                sum += this.sizes[i];
            }
            if(sum < 100)
                Vue.set(this.sizes, 0, this.sizes[0] + (100 - sum));
        }
    },
    computed: {
        dragging() {
            return this.resizeIdx != 0;
        }
    },
    mounted() {
        this.ensureSizesEqual100();
        this.$refs.container.addEventListener('mousemove', this.mouseMove);
    },
    created: function() {
        window.addEventListener('mouseup', this.mouseUp);
    },
    destroyed: function() {
        window.removeEventListener('mouseup', this.mouseUp);
    }
}
</script>
<style scoped>
.container {
    margin: 0;
    height: 100vh;
    width: 100%;
    padding: 0;
    margin: 0;
}
.dragging {
    user-select: none;
}
.horizontal > .splittable {
    display: inline-block;
}
.splittable {
    height: 100%;
    position: relative;
}
.resizer {
    position: absolute;
    left: -3px;
    height: 100px;
    width: 6px;
    z-index: 2;
    cursor: e-resize;
}
</style>