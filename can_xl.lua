
local can_xl_sdu_values =
{
	[0x00] = "Reserved",
	[0x01] = "Contend-based Adressing",
	[0x02] = "Reserved for future use",
	[0x03] = "Classical CAN/CAN FD mapped tunneling",
	[0x04] = "IEEE 802.3 (MAC frame) tunneling",
	[0x05] = "IEEE 802.3 (MAC frame) mapped tunneling",
	[0x06] = "Classical CAN mapped tunneling",
	[0x07] = "CAN FD mapped tunneling",
	[0xFF] = "Reserved"
}

local can_xl_protocol = Proto.new("can_xl", "CAN XL")

can_xl_protocol.fields.priority = ProtoField.uint16("can_xl.priority", "Priority", base.DEC_HEX)
can_xl_protocol.fields.vcid = ProtoField.uint8("can_xl.vcid", "VCID", base.DEC_HEX)
can_xl_protocol.fields.flags = ProtoField.uint8("can_xl.flags", "Flags", base.DEC_HEX)
can_xl_protocol.fields.sdu_type = ProtoField.uint8("can_xl.sdu_type", "SDU Type", base.DEC_HEX, can_xl_sdu_values)
can_xl_protocol.fields.length = ProtoField.uint16("can_xl.length", "Length", base.DEC_HEX)
can_xl_protocol.fields.acceptance_field = ProtoField.uint32("can_xl.acceptance_field", "Acceptance Field", base.DEC_HEX)
can_xl_protocol.fields.data = ProtoField.bytes("can_xl.data", "Data")

can_xl_protocol.fields.xlf_flag = ProtoField.uint8("can_xl.xlf_flag", "XLF Flag", base.DEC_HEX)
can_xl_protocol.fields.vcid_flag = ProtoField.uint8("can_xl.vcid_flag", "VCID Flag", base.DEC_HEX)
can_xl_protocol.fields.sec_flag = ProtoField.uint8("can_xl.sec_flag", "SEC Flag", base.DEC_HEX)

local CAN_XL_PRIORITY_OFFSET = 0
local CAN_XL_VCID_OFFSET = 2
local CAN_XL_FLAGS_OFFSET = 4
local CAN_XL_SDU_TYPE_OFFSET = 5
local CAN_XL_LENGTH_OFFSET = 6
local CAN_XL_ACCEPTANCE_FIELD_OFFSET = 8
local CAN_XL_DATA_OFFSET = 12

local CAN_XL_XLF_FLAG = 0x80
local CAN_XL_VCID_FLAG = 0x02
local CAN_XL_SEC_FLAG = 0x01

function can_xl_protocol.dissector(buffer, packet_info, tree)
    local buffer_length = buffer:len()
    packet_info.cols.protocol = "CAN XL"

    local can_xl_sub_tree = tree:add(can_xl_protocol, buffer)

    local priority_range = buffer(CAN_XL_PRIORITY_OFFSET, 2)
    local priority = bit.band(priority_range:le_uint(), 0x7FF)
    can_xl_sub_tree:add(can_xl_protocol.fields.priority, priority_range, priority)

    local vcid_range = buffer(CAN_XL_VCID_OFFSET, 1)
    local vcid = vcid_range:le_uint()
    can_xl_sub_tree:add(can_xl_protocol.fields.vcid, vcid_range, vcid)

    local flags_range = buffer(CAN_XL_FLAGS_OFFSET, 1)
    local flags = flags_range:le_uint();
    local flags_sub_tree = can_xl_sub_tree:add(can_xl_protocol.fields.flags, flags_range, flags)

    local xlf_flag = bit.band(flags, CAN_XL_XLF_FLAG)
    if xlf_flag > 0 then xlf_flag = 1 end
    flags_sub_tree:add(can_xl_protocol.fields.xlf_flag, flags_range, xlf_flag)

    local vcid_flag = bit.band(flags, CAN_XL_VCID_FLAG)
    if vcid_flag > 0 then vcid_flag = 1 end
    flags_sub_tree:add(can_xl_protocol.fields.vcid_flag, flags_range, vcid_flag)

    local sec_flag = bit.band(flags, CAN_XL_SEC_FLAG)
    if sec_flag > 0 then sec_flag = 1 end
    flags_sub_tree:add(can_xl_protocol.fields.sec_flag, flags_range, sec_flag)

    local sdu_type_range = buffer(CAN_XL_SDU_TYPE_OFFSET, 1)
    local sdu_type = sdu_type_range:le_uint()
    can_xl_sub_tree:add(can_xl_protocol.fields.sdu_type, sdu_type_range, sdu_type)

    local length_range = buffer(CAN_XL_LENGTH_OFFSET, 2)
    local length = length_range:le_uint()
    can_xl_sub_tree:add(can_xl_protocol.fields.length, length_range, length)

    local acceptance_field_range = buffer(CAN_XL_ACCEPTANCE_FIELD_OFFSET, 4)
    local acceptance_field = acceptance_field_range:le_uint()
    can_xl_sub_tree:add(can_xl_protocol.fields.acceptance_field, acceptance_field_range, acceptance_field)

    local data_range = buffer(CAN_XL_DATA_OFFSET, length)
    can_xl_sub_tree:add(can_xl_protocol.fields.data, data_range)

    local info_string = string.format(", Priority: %d (0x%X), VCID: %d (0x%X), Length: %d", priority, priority, vcid, vcid, length)
    packet_info.cols.info:append(info_string)
    can_xl_sub_tree:append_text(info_string)

    if xlf_flag > 0 then
        can_xl_sub_tree:append_text(", XLF")
    end
    if vcid_flag > 0 then
        can_xl_sub_tree:append_text(", VCID")
    end
    if sec_flag > 0 then
        can_xl_sub_tree:append_text(", SEC")
    end

    return buffer_length
end

function can_xl_protocol.init()
    -- Reset Data
end

local sll_type_dissector_table = DissectorTable.get("sll.ltype")
sll_type_dissector_table:add(0x000E, can_xl_protocol)
