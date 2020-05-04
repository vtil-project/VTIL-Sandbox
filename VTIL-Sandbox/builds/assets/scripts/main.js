function to_hex(num) {
    if(num < 0) {
        return "-0x" + (-num).toString(0x10)
    } else {
        return "+0x" + num.toString(0x10)
    }
}

function instruction_to_html(prev, ins) {
    let output = "";

    // Add virtual instruction pointer.
    //
    if(ins.is_pseudo)
        output += '<td class="ins-adr ins-adr-directive">Lifter Directive</td>'
    else
        output += '<td class="ins-adr ins-adr-vm-ip">' + ( ".VTIL" + ("000000000000000" + ins.vip.toString(16)).substr(-8) ) + '</td>'

    // Add stack pointer.
    //
    if(ins.sp_reset)
        output += '<td class="ins-sp ins-sp-reset">' + to_hex(ins.sp_offset) + '</td>'
    else if ( Number( prev != null ? prev.sp_offset : 0 ) == Number(ins.sp_offset) )
        output += '<td class="ins-sp ins-sp-balanced">' + to_hex(ins.sp_offset) + '</td>'
    else if ( Number( prev != null ? prev.sp_offset : 0 ) > Number(ins.sp_offset) )
        output += '<td class="ins-sp ins-sp-negative">' + to_hex(ins.sp_offset) + '</td>'
    else 
        output += '<td class="ins-sp ins-sp-positive">' + to_hex(ins.sp_offset) + '</td>'

    // Add opcode.
    //
    let ins_base = vtil.ins[ins.base];
    if(ins.explicit_volatile || ins_base.is_volatile)
        output += '<td class="ins-opcode ins-opcode-volatile">' + ins_base.name + '</td>'
    else
        output += '<td class="ins-opcode ins-opcode-default">' + ins_base.name + '</td>'

    // Add each operand.
    //
    for(let i in ins.operands)
    {
        // If it is a register:
        //
        let op = ins.operands[i];
        let op_text = op.text;
        if(op.type == "reg") {
            // Color based on whether it's a stack pointer, physical register or a virtual register.
            //
            if(op.is_stack_pointer)
                output += '<td class="ins-op ins-op-sp">' + op.text + '</td>'
            else if(op.is_physical)
                output += '<td class="ins-op ins-op-physical-reg">' + op.text + '</td>'
            else
                output += '<td class="ins-op ins-op-virtual-reg">' + op.text + '</td>'    
        } else {
            if(ins_base.memory_operand_index != -1 &&
               (ins_base.memory_operand_index + 1) == i &&
               (ins.operands[ins_base.memory_operand_index].is_stack_pointer)) 
            {
                if( op.i64 >= 0 )
                    output += '<td class="ins-op ins-op-sp-off-real">' + op.text + '</td>'  
                else
                    output += '<td class="ins-op ins-op-sp-off-virtual">' + op.text + '</td>'  

            } else {
                output += '<td class="ins-op ins-op-immediate">' + op.text + '</td>'   
            }
        }
    }

    return '<tr class="ins">' + output + '</tr>';
}

function render_block(dest, block) {
    let table_final = "";
    let prev = null;
    for(let i in block.stream) {
        table_final += instruction_to_html(prev, block.stream[i]);
        prev = block.stream[i];
    }
    
    table_final = '<table id="inst_table" class="basic-block">' + table_final + '</table>';
    document.getElementById(dest).innerHTML = table_final;
}