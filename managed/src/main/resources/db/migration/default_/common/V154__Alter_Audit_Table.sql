ALTER TABLE audit ADD COLUMN IF NOT EXISTS user_email varchar(255);
ALTER TABLE audit ADD COLUMN IF NOT EXISTS target TEXT;
ALTER TABLE audit ADD COLUMN IF NOT EXISTS target_uuid uuid;
ALTER TABLE audit ADD COLUMN IF NOT EXISTS action TEXT;