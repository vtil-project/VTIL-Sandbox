function enum_nav(fn) {
    const nav_buttons = document.getElementsByClassName("nav-button")
    for(let i = 0; i < nav_buttons.length; i++) {
        fn(nav_buttons[i])
    }
}

function update_buttons(view_name) {
    enum_nav((btn) => {
        btn.classList.remove("active")
        if(btn.getAttribute("data-dst") == view_name) {
            btn.classList.add("active")
        }
    })
}

enum_nav((btn) => {
    let dst = btn.getAttribute("data-dst");

    btn.onclick = () => {
        update_buttons(dst)
        vtil.set_view(dst)
    }

    if(dst == vtil.get_view()) {
        btn.classList.add("active")
    }
})