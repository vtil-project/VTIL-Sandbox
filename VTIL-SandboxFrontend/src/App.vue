<template>
<div id="main">
    <main-toolbar />
    <splittable-pane id="main-pane"/>
</div>
</template>
<script>
import MainToobar from './components/MainToolbar'
import SplittablePane from './components/SplittablePane'

export default {
    components: {
        "main-toolbar": MainToobar,
        SplittablePane
    }
}

</script>
<style>
body {
    margin: 0;
    font-family: -apple-system, 'Segoe UI', Ubuntu, Arial, sans-serif;
}
#main {
    height: 100%
}
#main-pane {
    position: absolute;
    top: 37px;
}
</style>
