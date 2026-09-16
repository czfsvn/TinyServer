-- ---------------------------------------------------------------------------
-- 本文件由 tinytools/xml2db.py 从 src/database/dbtables/tables.xml 自动生成
-- 请勿手工修改，改表请改 XML 后重新运行生成器
-- 生成时间: 2026-09-16 20:44:39
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `Stock` (
    `item_id`     VARCHAR(255)    NOT NULL COMMENT '物品id',
    `num`         BIGINT UNSIGNED NOT NULL COMMENT '数量',
    `weight`      DOUBLE          NOT NULL COMMENT '权重',
    `price`       FLOAT           NOT NULL COMMENT '价格',
    `description` VARCHAR(255)    NOT NULL COMMENT '描述',
    PRIMARY KEY (`item_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
